import gdspy
import re
import numpy as np
import argparse

def get_width_and_height(cell_name):
    cell = lib.cells[cell_name]
    rectangle_found = None
    polygons_by_spec = cell.get_polygons(by_spec=True)
    for (layer, datatype), polygons in polygons_by_spec.items():
        if layer == int(args.layer):
            for pts in polygons:
                if pts.shape[0] == 4:
                    rectangle_found = pts
                    break
            if rectangle_found is not None:
                break

    if rectangle_found is None:
        print("There is no valid rentangle.")

    else:
        xs = rectangle_found[:, 0]
        ys = rectangle_found[:, 1]
        width = np.max(xs) - np.min(xs)
        height = np.max(ys) - np.min(ys)
        area = width * height

        width = round(width, 4)
        height = round(height, 4)

    return width, height

def extract_pin_label_names(cell_name, target_texttype=251):
    label_names = set()
    cell = lib.cells[cell_name]
    for label in cell.labels:
        if label.texttype == target_texttype:
            label_names.add(label.text)
    return label_names

def separate_pins(pin_labels):
    input_pins = set()
    output_pins = set()

    for label in pin_labels:
        if re.match(r'^CO.+', label):
            output_pins.add(label)
        elif re.match(r'^C.+', label):
            input_pins.add(label)
    return input_pins, output_pins

def separate_pins(pin_labels):
    input_patterns = [r'^A.*', r'^B.*', r'^C.*', r'^D.*']
    output_patterns = [r'^CO.*', r'^Z.*', r'^Q.*', r'^S.*', r'^Y.*']

    input_pins = set()
    output_pins = set()

    pin_labels = {label for label in pin_labels if label.lower() not in {"vdd", "vss"}}

    for label in pin_labels:
        if any(re.match(pattern, label) for pattern in output_patterns):
            output_pins.add(label)
        elif any(re.match(pattern, label) for pattern in input_patterns):
            input_pins.add(label)

    classified = input_pins.union(output_pins)
    undefined_pins = pin_labels - classified

    if undefined_pins:
        print(f"Warning: Undefined pin labels: {', '.join(undefined_pins)}")

    return input_pins, output_pins
    

if __name__ == "__main__":
    parser = argparse.ArgumentParser()

    parser.add_argument('--gds', type=str, required=True, help="Please write the gds file that you want to extract. e.g. INV_X1.gds")
    parser.add_argument('--info_dir', type=str, required=True)
    parser.add_argument('--layer', type=int, default=100, help="Please write the \"cell boundary\" layer number. e.g. 100")
    parser.add_argument("--texttype", type=int, default=251, help="ÅØ½ºÆ® ¶óº§¿¡ ÇØ´çÇÏ´Â texttype (±âº»°ª: 251)")

    args = parser.parse_args()

    lib = gdspy.GdsLibrary(infile=f"{args.gds}")

    top_cells = lib.top_level()
    for top_cell in top_cells:
        top_cell_name = top_cell.name

        out_file = open(f"{args.info_dir}/{top_cell_name}.info", "w")

        out_file.write(f"{top_cell_name}\n")

        labels = extract_pin_label_names(top_cell_name, target_texttype=args.texttype)
        
        input_pins, output_pins = separate_pins(labels)
        out_file.write(" ".join(input_pins)+"\n")
        out_file.write(" ".join(output_pins)+"\n")

        width, height = get_width_and_height(top_cell_name)
        out_file.write(f"{width} {height}")

        out_file.close()
