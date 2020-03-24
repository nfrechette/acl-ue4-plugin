import csv
import sys


if __name__ == "__main__":
	if len(sys.argv) != 2:
		print('Invalid usage: python {}'.format(' '.join(sys.argv)))
		print('Usage: python tally_anim_csv.py <path/to/csv/file.csv>')
		sys.exit(1)

	csv_filename = sys.argv[1]

	rows = None

	# Read our CSV file and cache our rows
	with open(csv_filename, newline='') as csv_file:
		reader = csv.reader(csv_file)
		rows = list(reader)

	if not rows or len(rows) == 0:
		print('No CSV data found!')
		sys.exit(1)

	header_row = rows[0]
	if header_row[-1] == 'Animation/Total/ExtractPoseFromAnimData':
		print('This CSV file has already been processed!')
		sys.exit(1)

	# Process our rows
	# First append our new header to the first row as the last column

	num_columns = len(header_row)
	num_rows = len(rows)
	header_row.append('Animation/Total/ExtractPoseFromAnimData')
	print('Found {} columns and {} rows to process ...'.format(num_columns, num_rows))

	# Process every row and add the sum totals to our last column
	# Skip the first row, it contains our header
	# Skip the last two rows, it contains other things
	for row_idx in range(1, num_rows - 2):
		row = rows[row_idx]

		total_decomp_time = 0.0
		for column_idx in range(num_columns):
			header_name = header_row[column_idx]
			if header_name.endswith('ExtractPoseFromAnimData'):
				try:
					value = float(row[column_idx])
				except:
					value = 0.0
				total_decomp_time += value

		# By default, everything is in milliseconds
		row.append(str(total_decomp_time))

	# The second to last row must match our header
	rows[-2].append('Animation/Total/ExtractPoseFromAnimData')

	# Write out our modified rows over the same file
	with open(csv_filename, 'w', newline='') as csv_file:
		writer = csv.writer(csv_file, quoting=csv.QUOTE_MINIMAL)
		writer.writerows(rows)

	print('Done!')
