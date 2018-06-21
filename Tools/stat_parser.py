import multiprocessing
import os
import queue
import time
import sys

# This script depends on a SJSON parsing package:
# https://pypi.python.org/pypi/SJSON/1.1.0
# https://shelter13.net/projects/SJSON/
# https://bitbucket.org/Anteru/sjson/src
import sjson


def parse_argv():
	options = {}
	options['stats'] = ""
	options['csv_summary'] = False
	options['csv_error'] = False
	options['num_threads'] = 1

	for i in range(1, len(sys.argv)):
		value = sys.argv[i]

		# TODO: Strip trailing '/' or '\'
		if value.startswith('-stats='):
			options['stats'] = value[7:].replace('"', '')

		if value == '-csv_summary':
			options['csv_summary'] = True

		if value == '-csv_error':
			options['csv_error'] = True

		if value.startswith('-parallel='):
			options['num_threads'] = int(value[len('-parallel='):].replace('"', ''))

	if options['stats'] == None:
		print('Stat input directory not found')
		print_usage()
		sys.exit(1)

	if options['num_threads'] <= 0:
		print('-parallel switch argument must be greater than 0')
		print_usage()
		sys.exit(1)

	return options

def print_usage():
	print('Usage: python stat_parser.py -stats=<path to input directory for stats> [-csv_summary] [-csv_error]')

def bytes_to_mb(size_in_bytes):
	return size_in_bytes / (1024.0 * 1024.0)

def format_elapsed_time(elapsed_time):
	hours, rem = divmod(elapsed_time, 3600)
	minutes, seconds = divmod(rem, 60)
	return '{:0>2}h {:0>2}m {:05.2f}s'.format(int(hours), int(minutes), seconds)

def sanitize_csv_entry(entry):
	return entry.replace(', ', ' ').replace(',', '_')

def output_csv_summary(stat_dir, stats):
	csv_filename = os.path.join(stat_dir, 'stats_summary.csv')
	print('Generating CSV file {} ...'.format(csv_filename))
	file = open(csv_filename, 'w')

	header = 'Clip Name, Raw Size'
	if 'ue4_auto' in stats[0]:
		header += ', Auto Size, Auto Ratio, Auto Error'
	if 'ue4_acl' in stats[0]:
		header += ', ACL Size, ACL Ratio, ACL Error'
	print(header, file = file)

	for stat in stats:
		clip_name = stat['clip_name']
		raw_size = stat['acl_raw_size']
		csv_line = '{}, {}'.format(clip_name, raw_size)

		if 'ue4_auto' in stat:
			auto_size = stat['ue4_auto']['compressed_size']
			auto_ratio = stat['ue4_auto']['acl_compression_ratio']
			auto_error = stat['ue4_auto']['acl_max_error']
			csv_line += ', {}, {}, {}'.format(auto_size, auto_ratio, auto_error)

		if 'ue4_acl' in stat:
			acl_size = stat['ue4_acl']['compressed_size']
			acl_ratio = stat['ue4_acl']['acl_compression_ratio']
			acl_error = stat['ue4_acl']['acl_max_error']
			csv_line += ', {}, {}, {}'.format(acl_size, acl_ratio, acl_error)

		print(csv_line, file = file)

	file.close()

def output_csv_error(stat_dir, stats):
	if 'ue4_auto' in stats[0] and 'error_per_frame_and_bone' in stats[0]['ue4_auto']:
		csv_filename = os.path.join(stat_dir, 'stats_ue4_auto_error.csv')
		print('Generating CSV file {} ...'.format(csv_filename))
		file = open(csv_filename, 'w')

		print('Clip Name, Key Frame, Bone Index, Error', file = file)

		for stat in stats:
			name = stat['clip_name']
			key_frame = 0
			for frame_errors in stat['ue4_auto']['error_per_frame_and_bone']:
				bone_index = 0
				for bone_error in frame_errors:
					print('{}, {}, {}, {}'.format(name, key_frame, bone_index, bone_error), file = file)
					bone_index += 1

				key_frame += 1

		file.close()

	if 'ue4_acl' in stats[0] and 'error_per_frame_and_bone' in stats[0]['ue4_acl']:
		csv_filename = os.path.join(stat_dir, 'stats_ue4_acl_error.csv')
		print('Generating CSV file {} ...'.format(csv_filename))
		file = open(csv_filename, 'w')

		print('Clip Name, Key Frame, Bone Index, Error', file = file)

		for stat in stats:
			name = stat['clip_name']
			key_frame = 0
			for frame_errors in stat['ue4_acl']['error_per_frame_and_bone']:
				bone_index = 0
				for bone_error in frame_errors:
					print('{}, {}, {}, {}'.format(name, key_frame, bone_index, bone_error), file = file)
					bone_index += 1

				key_frame += 1

		file.close()

def print_progress(iteration, total, prefix='', suffix='', decimals = 1, bar_length = 50):
	# Taken from https://stackoverflow.com/questions/3173320/text-progress-bar-in-the-console
	"""
	Call in a loop to create terminal progress bar
	@params:
		iteration   - Required  : current iteration (Int)
		total       - Required  : total iterations (Int)
		prefix      - Optional  : prefix string (Str)
		suffix      - Optional  : suffix string (Str)
		decimals    - Optional  : positive number of decimals in percent complete (Int)
		bar_length  - Optional  : character length of bar (Int)
	"""
	str_format = "{0:." + str(decimals) + "f}"
	percents = str_format.format(100 * (iteration / float(total)))
	filled_length = int(round(bar_length * iteration / float(total)))
	bar = '█' * filled_length + '-' * (bar_length - filled_length)

	sys.stdout.write('\r%s |%s| %s%s %s' % (prefix, bar, percents, '%', suffix)),

	if iteration == total:
		sys.stdout.write('\n')
	sys.stdout.flush()

def append_stats(permutation, clip_stats, run_stats, aggregate_results):
	key = run_stats['desc']
	if not key in aggregate_results:
		run_total_stats = {}
		run_total_stats['desc'] = key
		run_total_stats['total_raw_size'] = 0
		run_total_stats['total_compressed_size'] = 0
		run_total_stats['total_compression_time'] = 0.0
		run_total_stats['max_error'] = 0.0
		run_total_stats['num_runs'] = 0
		aggregate_results[key] = run_total_stats

	run_total_stats = aggregate_results[key]
	run_total_stats['total_raw_size'] += clip_stats['acl_raw_size']
	run_total_stats['total_compressed_size'] += run_stats['compressed_size']
	run_total_stats['total_compression_time'] += run_stats['compression_time']
	run_total_stats['max_error'] = max(run_stats['acl_max_error'], run_total_stats['max_error'])
	run_total_stats['num_runs'] += 1

	if not permutation in aggregate_results:
		permutation_stats = {}
		permutation_stats['total_raw_size'] = 0
		permutation_stats['total_compressed_size'] = 0
		permutation_stats['total_compression_time'] = 0.0
		permutation_stats['max_error'] = 0.0
		permutation_stats['num_runs'] = 0
		permutation_stats['worst_error'] = -1.0
		permutation_stats['worst_entry'] = None
		aggregate_results[permutation] = permutation_stats

	permutation_stats = aggregate_results[permutation]
	permutation_stats['total_raw_size'] += clip_stats['acl_raw_size']
	permutation_stats['total_compressed_size'] += run_stats['compressed_size']
	permutation_stats['total_compression_time'] += run_stats['compression_time']
	permutation_stats['max_error'] = max(run_stats['acl_max_error'], permutation_stats['max_error'])
	permutation_stats['num_runs'] += 1
	if run_stats['acl_max_error'] > permutation_stats['worst_error']:
		permutation_stats['worst_error'] = run_stats['acl_max_error']
		permutation_stats['worst_entry'] = clip_stats

def do_parse_stats(options, stat_queue, result_queue):
	try:
		stats = []

		while True:
			stat_filename = stat_queue.get()
			if stat_filename is None:
				break

			with open(stat_filename, 'r') as file:
				try:
					file_data = sjson.loads(file.read())
					file_data['filename'] = stat_filename
					file_data['clip_name'] = os.path.splitext(os.path.basename(stat_filename))[0].replace('_stats', '')

					if not options['csv_error']:
						file_data['error_per_frame_and_bone'] = []

					stats.append(file_data)
				except sjson.ParseException:
					print('Failed to parse SJSON file: {}'.format(stat_filename))

			result_queue.put(('progress', stat_filename))

		result_queue.put(('done', stats))
	except KeyboardInterrupt:
		print('Interrupted')

def parallel_parse_stats(options, stat_files):
	stat_queue = multiprocessing.Queue()
	for stat_filename in stat_files:
		stat_queue.put(stat_filename)

	# Add a marker to terminate the jobs
	for i in range(options['num_threads']):
		stat_queue.put(None)

	result_queue = multiprocessing.Queue()

	jobs = [ multiprocessing.Process(target = do_parse_stats, args = (options, stat_queue, result_queue)) for _i in range(options['num_threads']) ]
	for job in jobs:
		job.start()

	num_stat_file_processed = 0
	stats = []
	print_progress(num_stat_file_processed, len(stat_files), 'Aggregating results:', '{} / {}'.format(num_stat_file_processed, len(stat_files)))
	try:
		while True:
			try:
				(msg, data) = result_queue.get(True, 1.0)
				if msg == 'progress':
					num_stat_file_processed += 1
					print_progress(num_stat_file_processed, len(stat_files), 'Aggregating results:', '{} / {}'.format(num_stat_file_processed, len(stat_files)))
				elif msg == 'done':
					stats.extend(data)
			except queue.Empty:
				all_jobs_done = True
				for job in jobs:
					if job.is_alive():
						all_jobs_done = False

				if all_jobs_done:
					break
	except KeyboardInterrupt:
		sys.exit(1)

	return stats

if __name__ == "__main__":
	options = parse_argv()

	stat_dir = options['stats']

	if not os.path.exists(stat_dir) or not os.path.isdir(stat_dir):
		print('Stats input directory not found: {}'.format(stat_dir))
		print_usage()
		sys.exit(1)

	stat_files = []

	for (dirpath, dirnames, filenames) in os.walk(stat_dir):
		for filename in filenames:
			if not filename.endswith('.sjson'):
				continue

			stat_filename = os.path.join(dirpath, filename)
			stat_files.append(stat_filename)

	if len(stat_files) == 0:
		sys.exit(0)

	aggregating_start_time = time.clock()

	stats = parallel_parse_stats(options, stat_files)

	aggregating_end_time = time.clock()
	print('Parsed stats in {}'.format(format_elapsed_time(aggregating_end_time - aggregating_start_time)))

	if options['csv_summary']:
		output_csv_summary(stat_dir, stats)

	if options['csv_error']:
		output_csv_error(stat_dir, stats)

	print()
	print('Stats per run type:')
	aggregate_results = {}
	num_acl_size_wins = 0
	num_acl_accuracy_wins = 0
	num_acl_speed_wins = 0
	num_acl_wins = 0
	num_acl_auto_wins = 0
	for stat in stats:
		if 'ue4_auto' in stat:
			ue4_auto = stat['ue4_auto']
			ue4_auto['desc'] = '{} {} {}'.format(ue4_auto['algorithm_name'], ue4_auto['rotation_format'], ue4_auto['translation_format'])
			append_stats('ue4_auto', stat, ue4_auto, aggregate_results)

		if 'ue4_acl' in stat:
			ue4_acl = stat['ue4_acl']
			ue4_acl['desc'] = ue4_acl['algorithm_name']
			append_stats('ue4_acl', stat, ue4_acl, aggregate_results)

		if 'ue4_auto' in stat and 'ue4_acl' in stat:
			ue4_auto = stat['ue4_auto']
			ue4_acl = stat['ue4_acl']
			if ue4_acl['compressed_size'] < ue4_auto['compressed_size']:
				num_acl_size_wins += 1
			if ue4_acl['acl_max_error'] < ue4_auto['acl_max_error']:
				num_acl_accuracy_wins += 1
			if ue4_acl['compression_time'] < ue4_auto['compression_time']:
				num_acl_speed_wins += 1
			if ue4_acl['compressed_size'] < ue4_auto['compressed_size'] and ue4_acl['acl_max_error'] < ue4_auto['acl_max_error'] and ue4_acl['compression_time'] < ue4_auto['compression_time']:
				num_acl_wins += 1

			lowers_error = ue4_acl['ue4_max_error'] < ue4_auto['ue4_max_error'];
			saved_size = int(ue4_auto['compressed_size']) - int(ue4_acl['compressed_size'])
			lowers_size = ue4_acl['compressed_size'] < ue4_auto['compressed_size'];
			error_under_threshold = float(ue4_acl['ue4_max_error']) <= 0.1;

			# keep it if it we want to force the error below the threshold and it reduces error
			# or if has an acceptable error and saves space
			# or if saves the same amount and an acceptable error that is lower than the previous best
			reduces_error_below_threshold = lowers_error and error_under_threshold;
			has_acceptable_error_and_saves_space = error_under_threshold and saved_size > 0;
			lowers_error_and_saves_same_or_better = error_under_threshold and lowers_error and saved_size >= 0;
			if reduces_error_below_threshold or has_acceptable_error_and_saves_space or lowers_error_and_saves_same_or_better:
				num_acl_auto_wins += 1

	print()
	raw_size = 0.0
	if 'ue4_auto' in aggregate_results:
		ue4_auto = aggregate_results['ue4_auto']
		raw_size = ue4_auto['total_raw_size']
		ratio = float(ue4_auto['total_raw_size']) / float(ue4_auto['total_compressed_size'])
		print('Total Automatic Compression:')
		print('Compressed {:.2f} MB, Elapsed {}, Ratio [{:.2f} : 1], Max error [{:.4f}]'.format(bytes_to_mb(ue4_auto['total_compressed_size']), format_elapsed_time(ue4_auto['total_compression_time']), ratio, ue4_auto['max_error']))
		print('Least accurate: {} Ratio: {:.2f}, Error: {:.4f}'.format(ue4_auto['worst_entry']['clip_name'], ue4_auto['worst_entry']['ue4_auto']['acl_compression_ratio'], ue4_auto['worst_entry']['ue4_auto']['acl_max_error']))
		print()

	if 'ue4_acl' in aggregate_results:
		ue4_acl = aggregate_results['ue4_acl']
		raw_size = ue4_acl['total_raw_size']
		ratio = float(ue4_acl['total_raw_size']) / float(ue4_acl['total_compressed_size'])
		print('Total ACL Compression:')
		print('Compressed {:.2f} MB, Elapsed {}, Ratio [{:.2f} : 1], Max error [{:.4f}]'.format(bytes_to_mb(ue4_acl['total_compressed_size']), format_elapsed_time(ue4_acl['total_compression_time']), ratio, ue4_acl['max_error']))
		print('Least accurate: {} Ratio: {:.2f}, Error: {:.4f}'.format(ue4_acl['worst_entry']['clip_name'], ue4_acl['worst_entry']['ue4_acl']['acl_compression_ratio'], ue4_acl['worst_entry']['ue4_acl']['acl_max_error']))
		print()

	print('Raw size: {:.2f} MB'.format(bytes_to_mb(raw_size)))
	print('ACL was smaller for {} clips ({:.2f} %)'.format(num_acl_size_wins, float(num_acl_size_wins) / float(len(stats)) * 100.0))
	print('ACL was more accurate for {} clips ({:.2f} %)'.format(num_acl_accuracy_wins, float(num_acl_accuracy_wins) / float(len(stats)) * 100.0))
	print('ACL has faster compression for {} clips ({:.2f} %)'.format(num_acl_speed_wins, float(num_acl_speed_wins) / float(len(stats)) * 100.0))
	print('ACL was smaller, better, faster for {} clips ({:.2f} %)'.format(num_acl_wins, float(num_acl_wins) / float(len(stats)) * 100.0))
	print('ACL won with simulated auto {} clips ({:.2f} %)'.format(num_acl_auto_wins, float(num_acl_auto_wins) / float(len(stats)) * 100.0))
