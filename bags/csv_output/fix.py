import csv

# Input and output file names
input_file = 'run2_motor.csv'
output_file = 'run2_motor_fixed.csv'

# Open the input and output files
with open(input_file, 'r', newline='') as infile, open(output_file, 'w', newline='') as outfile:
    reader = csv.reader(infile)
    writer = csv.writer(outfile)

    for row in reader:
        # row is a list with a single string, e.g., ['"43667,0.00000,24.99925"']
        if row:
            # Remove quotes and split by comma
            values = row[0].strip('"').split(',')
            writer.writerow(values)

print("Conversion complete. Output written to", output_file)
