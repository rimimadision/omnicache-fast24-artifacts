import os
import csv
import re

#/localhome/jian/CompoundFS-dev/AE-RESULTS-PERFOPT/leveldb/figure13/fusionfs/result.txt

# Define the arrays
thread_arr = [16, 32]
#workload_arr = ["pvt_seq", "pvt_rand"]
workload_arr = ["ycsba", "ycsbb", "ycsbc", "ycsbd", "ycsbe", "ycsbf"]
config_arr = ["fusionfs", "hostcache", "omnicache"]
config_out_arr = ["FusionFS", "HostCache-user-level", "OmniCache"]
readsize_arr = ["128"]

# Base directory for output files
output_dir = os.environ.get("AERESULTS", "")
base_dir_template = f"{output_dir}/ycsb/{{config}}/result.txt"

# Output CSV file
output_file = "RESULT.csv"

def extract_and_round_ops_per_sec(line, operation_type):
#    if operation_type == "fillrandom":
#        pattern = re.compile(r'fillrandom\s+:\s+(\d+\.\d+)\s+micros/op;\s+(\d+\.\d+)\sMB/s')
#    elif operation_type == "readrandom":
#        pattern = re.compile(r'readrandom\s+:\s+(\d+\.\d+)\s+micros/op;\s+(\d+\.\d+)\sMB/s\s+\(.*\)')
#    else:
#        return None

    #pattern = re.compile(r'\d+\.\d+ MB/s')
    match =  re.search(r'(\d+\.\d+)\sMB/s', line) 

    if match:
        mbps = float(match.group(1))
        return mbps 
    else:
        return None

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

                workload_data = [workload]
                for config in config_arr: 

                    base_dir = base_dir_template.format(config=config)

                    #file_path = os.path.join(base_dir, f"result.txt")
                    file_path = base_dir
                    if os.path.exists(file_path):
                        with open(file_path, 'r') as file:
                            lines = file.readlines()
                            ops_sec_found = False
                            for line in lines:
                                if "MB/s" in line and workload in line:
                                    #  print(line)
                                    ops_sec_value = extract_and_round_ops_per_sec(line, workload)
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
