#!/usr/bin/env python3
from __future__ import annotations

import z3

def parse_canonical(key_str):
    if not isinstance(key_str, str):
        raise TypeError(
            f"Canonical key must be str, got {type(key_str).__name__}: {key_str!r}"
        )
    s = key_str.strip()
    if not s.startswith('0b'):
        raise ValueError(
            f"Canonical key must start with '0b': {key_str!r}"
        )
    bits = s[2:]
    if not bits:
        raise ValueError(f"Canonical key has empty bit string: {key_str!r}")
    for ch in bits:
        if ch not in '01':
            raise ValueError(
                f"Canonical key has non-binary char {ch!r}: {key_str!r}"
            )
    n_bits = len(bits)
    if n_bits & (n_bits - 1) != 0:
        raise ValueError(
            f"Truth-table length {n_bits} is not a power of 2: {key_str!r}"
        )
    n_inputs = n_bits.bit_length() - 1
    tt_bits = [int(b) for b in bits]
    return n_inputs, tt_bits

def canonical_from_bits(tt_bits):
    return '0b' + ''.join(str(b) for b in tt_bits)

def _pi_value_at(pi_idx, x_pattern, n_inputs):
    return (x_pattern >> (n_inputs - 1 - pi_idx)) & 1

def _matches_projection(tt_bits, n_inputs, pi_idx):
    for p in range(1 << n_inputs):
        if tt_bits[p] != _pi_value_at(pi_idx, p, n_inputs):
            return False
    return True

def _is_affine(tt_bits, n_inputs):
    n_bits = 1 << n_inputs
    a = list(tt_bits)
    for i in range(n_inputs):
        step = 1 << i
        for j in range(n_bits):
            if (j >> i) & 1:
                a[j] ^= a[j ^ step]
    for s in range(n_bits):
        if a[s] == 1 and bin(s).count('1') > 1:
            return False
    return True

def _base_output_expr(base_tt, base_n, port_exprs):
    minterms = []
    for x in range(1 << base_n):
        if base_tt[x] != 1:
            continue
        literals = []
        for j in range(base_n):
            bit_j = (x >> (base_n - 1 - j)) & 1
            literals.append(port_exprs[j] if bit_j == 1
                            else z3.Not(port_exprs[j]))
        if len(literals) == 1:
            minterms.append(literals[0])
        else:
            minterms.append(z3.And(*literals))
    if not minterms:
        return z3.BoolVal(False)
    if len(minterms) == 1:
        return minterms[0]
    return z3.Or(*minterms)

def _try_synthesis(target_tt, target_n, base_tt, base_n, total_gates,
                   timeout_ms, return_circuit=False):
    opt = z3.Optimize()
    opt.set('timeout', timeout_ms)

    n_pi = target_n
    n_patterns = 1 << target_n

    is_inv = [z3.Bool(f'inv_{i}') for i in range(total_gates)]
    selector = [[z3.Int(f'sel_{i}_{j}') for j in range(base_n)]
                for i in range(total_gates)]

    for i in range(total_gates):
        n_avail = n_pi + i
        for j in range(base_n):
            opt.add(selector[i][j] >= 0)
            opt.add(selector[i][j] < n_avail)

    for i in range(total_gates):
        for j in range(base_n):
            for k in range(j + 1, base_n):
                opt.add(z3.Implies(z3.Not(is_inv[i]),
                                   selector[i][j] != selector[i][k]))

    gate_val = [[z3.Bool(f'g{i}_x{x}') for x in range(n_patterns)]
                for i in range(total_gates)]

    for x in range(n_patterns):
        def signal_val_at_x(k, _x=x):
            if k < n_pi:
                return z3.BoolVal(bool(_pi_value_at(k, _x, target_n)))
            return gate_val[k - n_pi][_x]

        for i in range(total_gates):
            n_avail = n_pi + i
            port_exprs = []
            for j in range(base_n):
                expr = z3.BoolVal(False)
                for k in range(n_avail - 1, -1, -1):
                    expr = z3.If(selector[i][j] == k,
                                 signal_val_at_x(k), expr)
                port_exprs.append(expr)

            base_out = _base_output_expr(base_tt, base_n, port_exprs)
            inv_out = z3.Not(port_exprs[0])
            opt.add(gate_val[i][x] == z3.If(is_inv[i], inv_out, base_out))

    last_gate = total_gates - 1
    for x in range(n_patterns):
        last_val = gate_val[last_gate][x]
        if target_tt[x] == 1:
            opt.add(last_val)
        else:
            opt.add(z3.Not(last_val))

    for i in range(total_gates - 1):
        feeds_later = []
        for later in range(i + 1, total_gates):
            for j in range(base_n):
                feeds_later.append(selector[later][j] == n_pi + i)
        if feeds_later:
            opt.add(z3.Or(*feeds_later))

    inv_count_var = z3.Sum([z3.If(is_inv[i], 1, 0)
                            for i in range(total_gates)])
    opt.minimize(inv_count_var)

    result = opt.check()
    if result == z3.unknown:
        return 'TIMEOUT'
    if result == z3.unsat:
        return None

    m = opt.model()
    inv_n = sum(1 for i in range(total_gates)
                if z3.is_true(m.eval(is_inv[i])))
    base_n_used = total_gates - inv_n
    if not return_circuit:
        return (base_n_used, inv_n)
    gates = []
    for i in range(total_gates):
        inv_flag = bool(z3.is_true(m.eval(is_inv[i])))
        ports = [m.eval(selector[i][j]).as_long() for j in range(base_n)]
        gates.append({'is_inv': inv_flag, 'inputs': ports})
    return (base_n_used, inv_n, {'gates': gates})

def synthesize_and_extract(target_tt, target_n, base_tt, base_n,
                           total_gates, timeout_s=60):
    r = _try_synthesis(target_tt, target_n, base_tt, base_n,
                       total_gates, timeout_ms=timeout_s * 1000,
                       return_circuit=True)
    if r == 'TIMEOUT':
        return {'timeout': True}
    if r is None:
        return {'sat': False}
    bc, ic, circuit = r
    return {'sat': True, 'base_count': bc, 'inv_count': ic,
            'circuit': circuit}

def simulate_circuit(circuit, target_n, base_tt, base_n):
    gates = circuit['gates']
    n_patterns = 1 << target_n
    out_tt = [0] * n_patterns
    for x in range(n_patterns):
        signals = [_pi_value_at(k, x, target_n) for k in range(target_n)]
        for g in gates:
            in_vals = [signals[k] for k in g['inputs']]
            if g['is_inv']:
                v = 1 - in_vals[0]
            else:
                pos = 0
                for b in in_vals:
                    pos = (pos << 1) | b
                v = base_tt[pos]
            signals.append(v)
        out_tt[x] = signals[-1]
    return out_tt

def find_decomposition(target_tt, target_n, base_tt, base_n,
                       max_gates=30, timeout_s=60,
                       disable_affine_check=False):
    if not isinstance(target_n, int) or target_n < 1:
        raise ValueError(f"target_n must be int >=1, got {target_n!r}")
    if not isinstance(base_n, int) or base_n < 2:
        raise ValueError(
            f"base_n must be int >=2 (BUF/INV bases are not allowed): {base_n!r}"
        )
    if target_n > 4:
        raise ValueError(
            f"target_n > 4 is out of project scope (got {target_n}). "
            "Increase the cap explicitly if you really mean it."
        )
    if len(target_tt) != (1 << target_n):
        raise ValueError(
            f"target_tt length {len(target_tt)} != 2^{target_n}"
        )
    if len(base_tt) != (1 << base_n):
        raise ValueError(
            f"base_tt length {len(base_tt)} != 2^{base_n}"
        )

    for pi in range(target_n):
        if _matches_projection(target_tt, target_n, pi):
            return {'status': 'PROJECTION', 'pi_idx': pi,
                    'base_count': 0, 'inv_count': 0, 'total': 0}

    if not disable_affine_check:
        if _is_affine(base_tt, base_n) and not _is_affine(target_tt, target_n):
            return {'status': 'IMPOSSIBLE_AFFINE',
                    'reason': 'base+INV is closed under affine class L '
                              "(Post's lattice); target is non-affine"}

    for total_gates in range(1, max_gates + 1):
        r = _try_synthesis(target_tt, target_n, base_tt, base_n,
                           total_gates, timeout_ms=timeout_s * 1000)
        if r == 'TIMEOUT':
            return {'status': 'TIMEOUT', 'tried_gates': total_gates}
        if r is not None:
            base_count, inv_count = r
            return {'status': 'OK',
                    'base_count': base_count,
                    'inv_count': inv_count,
                    'total': total_gates}

    return {'status': 'IMPOSSIBLE_MAXGATES', 'max_gates_tried': max_gates}
