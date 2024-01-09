import os
import csv
import re

# Define the arrays
thread_arr = [16, 32]
#workload_arr = ["pvt_seq", "pvt_rand"]
workload_arr = ["knn"]
config_arr = ["fusionfs", "hostcache", "omnicache"]
config_out_arr = ["FusionFS", "HostCache-user-level", "OmniCache"]
readsize_arr = ["128"]

# Base directory for output files
output_dir = os.environ.get("AERESULTS", "")
base_dir_template = f"{output_dir}/knn/figure12b/{{config}}/knn/{{thd}}/"

# Output CSV file
output_file = "RESULT.csv"

# Function to extract the value before "MB/s" from a line and round to the nearest integer
def extract_and_round_ops_per_sec(line):
    pattern = re.compile(r'(\d+\.\d+)\sGB/s')

    match = pattern.search(line)

    if match:
        number_before_gb = float(match.group(1))
        #  print(number_before_gb)
    else:
        print("No match found.")
    #parts = line.split()
    #ops_index = parts.index("GB/s")
    #ops_sec_value = float(parts[ops_index - 1])
    return number_before_gb

# Main function to iterate through workloads and extract MB/s
def main():
    with open(output_file, mode='w', newline='') as csv_file:
        csv_writer = csv.writer(csv_file)

        for access_pattern in workload_arr:
            SPACE =[f"-----------------------"]
            csv_writer.writerow(SPACE)
            header_row = [f"Table for {access_pattern}"]
            csv_writer.writerow(header_row)
            csv_writer.writerow(SPACE)

            #header_row = ["rsize"] + config_out_arr
            header_row = ["Threads"] + config_out_arr
            csv_writer.writerow(header_row)

            #for readsize in readsize_arr:
            for workload in workload_arr:
                if workload != access_pattern:
                    continue  # Skip if not the desired access pattern

                #workload_data = [readsize]

                for thd in thread_arr:

                    workload_data = [thd]
                    for config in config_arr: 

                        base_dir = base_dir_template.format(workload=workload, config=config, thd=thd)

                        file_path = os.path.join(base_dir, f"result.txt")
                        if os.path.exists(file_path):
                            with open(file_path, 'r') as file:
                                lines = file.readlines()
                                ops_sec_found = False
                                for line in lines:
                                    if "GB/s" in line:
                                        ops_sec_value = extract_and_round_ops_per_sec(line)
                                        workload_data.append(ops_sec_value)
                                        ops_sec_found = True
                                        break
                                if not ops_sec_found:
                                    workload_data.append("N/A")
                        else:
                            print(f"File not found: {file_path}")

                    csv_writer.writerow(workload_data)

if __name__ == "__main__":
    main()
