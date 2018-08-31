import os
import re
import sys


if __name__ == "__main__":
	if len(sys.argv) != 2:
		print('Invalid usage: python {}'.format(' '.join(sys.argv)))
		print('Usage: python clean_log.py <path/to/log/file.txt>')
		sys.exit(1)

	log_filename = sys.argv[1]
	with open(log_filename, 'r') as file:
		file_content = file.read()
		matches = re.findall('^LogPlayLevel: UE4Game: LogAnimation:Warning: \[\[(.+)\]\].*', file_content, re.MULTILINE)
		if len(matches) == 0:
			matches = re.findall('^\[.+\]\[.+\]LogAnimation:Warning: \[\[(.+)\]\].*', file_content, re.MULTILINE)
		if len(matches) == 0:
			matches = re.findall('^\[.+\]\[.+\]LogPlayLevel: \[.+\]\[.+\]LogAnimation: Warning: \[\[(.+)\]\].*', file_content, re.MULTILINE)
		if len(matches) == 0:
			matches = re.findall('^\[.+\]\[.+\]LogAnimation: Warning: \[\[(.+)\]\].*', file_content, re.MULTILINE)

		print('Found {} CSV lines'.format(len(matches)))
		if len(matches) > 0:
			csv_filename = os.path.splitext(log_filename)[0] + '.csv'
			with open(csv_filename, 'w') as csv_file:
				print('Long Clip Name,Sample Time, Decompression Time (us), Decompression Speed (MB/sec), Codec, Clip Name', file = csv_file)
				for line in matches:
					values = line.split(',')
					short_name = values[0].split('.')[1]
					print('{},{}'.format(line, short_name), file = csv_file)

