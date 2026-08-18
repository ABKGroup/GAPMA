import csv
import hashlib
import tempfile
import unittest
from pathlib import Path

from flow.scripts.datagen.build_mining_edge_features import (
    aggregate_pmnpn_counts,
    flatten_minimum_cost_p_to_np,
    minimum_cost_p_to_np,
    process_run,
)
from tools.mining.scripts.demand_predictor.train.dataset_diff import _read_p_to_np_map


class PmNpnTieTests(unittest.TestCase):
    def setUp(self):
        self.rows = {
            ("P1", "NP_B", 0, 1),
            ("P1", "NP_A", 1, 0),
            ("P1", "NP_C", 2, 0),
            ("P2", "NP_A", 0, 0),
        }

    def test_minimum_cost_relation_preserves_all_ties(self):
        self.assertEqual(
            minimum_cost_p_to_np(self.rows),
            {
                "P1": (("NP_A", 1, 0), ("NP_B", 0, 1)),
                "P2": (("NP_A", 0, 0),),
            },
        )

    def test_mining_counts_are_added_to_every_tied_representative(self):
        relation = minimum_cost_p_to_np(self.rows)
        self.assertEqual(
            aggregate_pmnpn_counts([("P1", 3), ("P2", 5)], relation),
            [("NP_A", 8), ("NP_B", 3)],
        )

    def test_global_relation_preserves_ties_in_stable_order(self):
        self.assertEqual(
            flatten_minimum_cost_p_to_np(self.rows),
            [
                ("P1", "NP_A", 1, 0),
                ("P1", "NP_B", 0, 1),
                ("P2", "NP_A", 0, 0),
            ],
        )

    def test_gat_loader_selects_stable_minimum_from_relation(self):
        with tempfile.TemporaryDirectory() as tmp:
            csv_path = Path(tmp) / "global_p_to_np.csv"
            csv_path.write_text(
                "p_canonical,np_canonical,erased_input_inv,erased_output_inv\n"
                "P1,NP_B,0,1\n"
                "P1,NP_A,1,0\n"
                "P1,NP_C,2,0\n"
                "P2,NP_A,0,0\n"
            )
            self.assertEqual(
                _read_p_to_np_map(str(csv_path)),
                {
                    "P1": ("NP_A", 1, 0),
                    "P2": ("NP_A", 0, 0),
                },
            )

    def test_process_run_writes_duplicate_count_to_each_tied_representative(self):
        with tempfile.TemporaryDirectory() as tmp:
            run_dir = Path(tmp)
            (run_dir / "pattern_instances.out").write_text(
                "canonical_key=P1\n"
                "clst1: 1\n"
                "clst2: 1\n"
                "canonical_key=P2\n"
                "clst3: 1\n"
            )
            (run_dir / "cell_id_map.csv").write_text(
                "cell_id,master_cell\n1,CELL1\n"
            )
            (run_dir / "p_to_np_class.csv").write_text(
                "p_canonical_key,np_canonical_key,erased_input_inverters,"
                "erased_output_inverters\n"
                "P1,NP_B,0,1\n"
                "P1,NP_A,1,0\n"
                "P1,NP_C,2,0\n"
                "P2,NP_A,0,0\n"
            )

            process_run(run_dir)

            with open(run_dir / "mining_node_rank.csv") as fp:
                rows = list(csv.DictReader(fp))
            self.assertEqual(
                [(row["canonical"], int(row["cluster_count"])) for row in rows],
                [("NP_A", 3), ("NP_B", 2)],
            )

    def test_shipped_relation_matches_regenerated_four_input_table(self):
        repo = Path(__file__).resolve().parents[3]
        csv_path = repo / "data" / "mining" / "global_p_to_np.csv"
        digest = hashlib.sha256(csv_path.read_bytes()).hexdigest()
        self.assertEqual(
            digest,
            "96f281d38afec2108e88e04ec738b3e915212376a9164d8599dbeaa01a6c0616",
        )
        with open(csv_path) as fp:
            rows = list(csv.DictReader(fp))
        self.assertEqual(len(rows), 7204)
        self.assertEqual(len({row["p_canonical"] for row in rows}), 4080)


if __name__ == "__main__":
    unittest.main()
