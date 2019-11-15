import multiprocessing
import numpy
import os
import platform
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
	options['acl_stats'] = ""
	options['ue4_stats'] = ""
	options['dual_stat_inputs'] = False
	options['csv_summary'] = False
	options['csv_error'] = False
	options['csv_kr'] = False
	options['num_threads'] = 1

	for i in range(1, len(sys.argv)):
		value = sys.argv[i]

		# TODO: Strip trailing '/' or '\'
		if value.startswith('-stats='):
			options['stats'] = value[7:].replace('"', '')

		# TODO: Strip trailing '/' or '\'
		if value.startswith('-acl='):
			options['acl_stats'] = value[5:].replace('"', '')

		# TODO: Strip trailing '/' or '\'
		if value.startswith('-ue4='):
			options['ue4_stats'] = value[5:].replace('"', '')

		if value == '-csv_summary':
			options['csv_summary'] = True

		if value == '-csv_error':
			options['csv_error'] = True

		if value == '-csv_kr':
			options['csv_kr'] = True

		if value.startswith('-parallel='):
			options['num_threads'] = int(value[len('-parallel='):].replace('"', ''))

	has_stats_dir = (not options['stats'] == None) and (not len(options['stats']) == 0)
	has_acl_stats_dir = (not options['acl_stats'] == None) and (not len(options['acl_stats']) == 0)
	has_ue4_stats_dir = (not options['ue4_stats'] == None) and (not len(options['ue4_stats']) == 0)
	if not has_stats_dir and not has_acl_stats_dir and not has_ue4_stats_dir:
		print('A stats input directory must be provided either with `-stats=` or with both `-acl=` and `-ue4=`')
		print_usage()
		sys.exit(1)

	if has_stats_dir and (has_acl_stats_dir or has_ue4_stats_dir):
		print('`-stats=` cannot be used with `-acl=` or `-ue4=`')
		print_usage()
		sys.exit(1)

	if not has_stats_dir and not (has_acl_stats_dir and has_ue4_stats_dir):
		print('Both `-acl=` and `-ue4=` must be provided together')
		print_usage()
		sys.exit(1)

	options['dual_stat_inputs'] = has_acl_stats_dir and has_ue4_stats_dir

	if has_acl_stats_dir and has_ue4_stats_dir:
		if not os.path.exists(options['acl_stats']) or not os.path.isdir(options['acl_stats']):
			print('ACL stats input directory not found: {}'.format(options['acl_stats']))
			print_usage()
			sys.exit(1)

		if not os.path.exists(options['ue4_stats']) or not os.path.isdir(options['ue4_stats']):
			print('UE4 stats input directory not found: {}'.format(options['ue4_stats']))
			print_usage()
			sys.exit(1)
	else:
		if not os.path.exists(options['stats']) or not os.path.isdir(options['stats']):
			print('Stats input directory not found: {}'.format(options['stats']))
			print_usage()
			sys.exit(1)

	if options['num_threads'] <= 0:
		print('-parallel switch argument must be greater than 0')
		print_usage()
		sys.exit(1)

	return options

def print_usage():
	print('Usage: python stat_parser.py [-stats=<path to input directory for stats>] [-acl=<path to acl stats>] [-ue4=<path to ue4 stats>] [-csv_summary] [-csv_error] [-csv_kr] [-parallel=<num threads>]')

def bytes_to_mb(size_in_bytes):
	return size_in_bytes / (1024.0 * 1024.0)

def bytes_to_kb(size_in_bytes):
	return size_in_bytes / 1024.0

def format_elapsed_time(elapsed_time):
	hours, rem = divmod(elapsed_time, 3600)
	minutes, seconds = divmod(rem, 60)
	return '{:0>2}h {:0>2}m {:05.2f}s'.format(int(hours), int(minutes), seconds)

def sanitize_csv_entry(entry):
	return entry.replace(', ', ' ').replace(',', '_')

def output_csv_summary(stat_dir, merged_stats):
	csv_filename = os.path.join(stat_dir, 'stats_summary.csv')
	print('Generating CSV file {} ...'.format(csv_filename))
	file = open(csv_filename, 'w')

	stat_acl, stat_auto = merged_stats[0]
	header = 'Clip Name, Raw Size'
	if 'ue4_auto' in stat_auto:
		header += ', Auto Size, Auto Ratio, Auto UE4 Error, Auto ACL Error'
	if 'ue4_acl' in stat_acl:
		header += ', ACL Size, ACL Ratio, ACL UE4 Error, ACL ACL Error'
	print(header, file = file)

	for (stat_acl, stat_auto) in merged_stats:
		clip_name = stat_acl['clip_name']
		raw_size = stat_acl['acl_raw_size']
		csv_line = '{}, {}'.format(clip_name, raw_size)

		if 'ue4_auto' in stat_auto:
			auto_size = stat_auto['ue4_auto']['compressed_size']
			auto_ratio = stat_auto['ue4_auto']['acl_compression_ratio']
			auto_ue4_error = stat_auto['ue4_auto']['ue4_max_error']
			auto_acl_error = stat_auto['ue4_auto']['acl_max_error']
			csv_line += ', {}, {}, {}, {}'.format(auto_size, auto_ratio, auto_ue4_error, auto_acl_error)

		if 'ue4_acl' in stat_acl:
			acl_size = stat_acl['ue4_acl']['compressed_size']
			acl_ratio = stat_acl['ue4_acl']['acl_compression_ratio']
			acl_ue4_error = stat_acl['ue4_acl']['ue4_max_error']
			acl_acl_error = stat_acl['ue4_acl']['acl_max_error']
			csv_line += ', {}, {}, {}, {}'.format(acl_size, acl_ratio, acl_ue4_error, acl_acl_error)

		print(csv_line, file = file)

	file.close()

def output_csv_error(stat_dir, merged_stats):
	stat_acl, stat_auto = merged_stats[0]
	if 'ue4_auto' in stat_auto and 'error_per_frame_and_bone' in stat_auto['ue4_auto']:
		csv_filename = os.path.join(stat_dir, 'stats_ue4_auto_error.csv')
		print('Generating CSV file {} ...'.format(csv_filename))
		file = open(csv_filename, 'w')

		print('Clip Name, Key Frame, Bone Index, Error', file = file)

		for (_, stat_auto) in merged_stats:
			name = stat_auto['clip_name']
			key_frame = 0
			for frame_errors in stat_auto['ue4_auto']['error_per_frame_and_bone']:
				bone_index = 0
				for bone_error in frame_errors:
					print('{}, {}, {}, {}'.format(name, key_frame, bone_index, bone_error), file = file)
					bone_index += 1

				key_frame += 1

		file.close()

	if 'ue4_acl' in stat_acl and 'error_per_frame_and_bone' in stat_acl['ue4_acl']:
		csv_filename = os.path.join(stat_dir, 'stats_ue4_acl_error.csv')
		print('Generating CSV file {} ...'.format(csv_filename))
		file = open(csv_filename, 'w')

		print('Clip Name, Key Frame, Bone Index, Error', file = file)

		for (stat_acl, _) in merged_stats:
			name = stat_acl['clip_name']
			key_frame = 0
			for frame_errors in stat_acl['ue4_acl']['error_per_frame_and_bone']:
				bone_index = 0
				for bone_error in frame_errors:
					print('{}, {}, {}, {}'.format(name, key_frame, bone_index, bone_error), file = file)
					bone_index += 1

				key_frame += 1

		file.close()

def output_csv_kr(stat_dir, clip_drop_rates, pose_drop_rates, track_drop_rates):
	csv_filename = os.path.join(stat_dir, 'stats_kr.csv')
	print('Generating CSV file {} ...'.format(csv_filename))
	file = open(csv_filename, 'w')

	print('Dropped Per Clip, Dropped Per Pose, Dropped Per Track', file = file)

	num_rows = max([len(clip_drop_rates), len(pose_drop_rates), len(track_drop_rates)])
	values = [('', '', '')] * num_rows

	for i in range(len(clip_drop_rates)):
		clip_rate = clip_drop_rates[i]
		_, pose_rate, track_rate = values[i]
		values[i] = (clip_rate, pose_rate, track_rate)

	for i in range(len(pose_drop_rates)):
		pose_rate = pose_drop_rates[i]
		clip_rate, _, track_rate = values[i]
		values[i] = (clip_rate, pose_rate, track_rate)

	for i in range(len(track_drop_rates)):
		track_rate = track_drop_rates[i]
		clip_rate, pose_rate, _ = values[i]
		values[i] = (clip_rate, pose_rate, track_rate)

	for (clip_rate, pose_rate, track_rate) in values:
		print('{}, {}, {}'.format(clip_rate, pose_rate, track_rate), file = file)

	file.close()

def print_progress(iteration, total, prefix='', suffix='', decimals = 1, bar_length = 40):
	# Taken from https://stackoverflow.com/questions/3173320/text-progress-bar-in-the-console
	# With minor tweaks
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

	# We need to clear any previous line we might have to ensure we have no visual artifacts
	# Note that if this function is called too quickly, the text might flicker
	terminal_width = 80
	sys.stdout.write('{}\r'.format(' ' * terminal_width))
	sys.stdout.flush()

	sys.stdout.write('%s |%s| %s%s %s\r' % (prefix, bar, percents, '%', suffix)),
	sys.stdout.flush()

	if iteration == total:
		sys.stdout.write('\n')

def append_stats(permutation, clip_stats, run_stats, aggregate_results):
	key = run_stats['desc']
	if not key in aggregate_results:
		run_total_stats = {}
		run_total_stats['desc'] = key
		run_total_stats['total_raw_size'] = 0
		run_total_stats['total_compressed_size'] = 0
		run_total_stats['total_compression_time'] = 0.0
		run_total_stats['acl_max_error'] = 0.0
		run_total_stats['ue4_max_error'] = 0.0
		run_total_stats['num_runs'] = 0
		aggregate_results[key] = run_total_stats

	run_total_stats = aggregate_results[key]
	run_total_stats['total_raw_size'] += clip_stats['acl_raw_size']
	run_total_stats['total_compressed_size'] += run_stats['compressed_size']
	run_total_stats['total_compression_time'] += run_stats['compression_time']
	run_total_stats['acl_max_error'] = max(run_stats['acl_max_error'], run_total_stats['acl_max_error'])
	run_total_stats['ue4_max_error'] = max(run_stats['ue4_max_error'], run_total_stats['ue4_max_error'])
	run_total_stats['num_runs'] += 1

	if not permutation in aggregate_results:
		permutation_stats = {}
		permutation_stats['total_raw_size'] = 0
		permutation_stats['total_compressed_size'] = 0
		permutation_stats['total_compression_time'] = 0.0
		permutation_stats['acl_max_error'] = 0.0
		permutation_stats['ue4_max_error'] = 0.0
		permutation_stats['num_runs'] = 0
		permutation_stats['worst_error'] = -1.0
		permutation_stats['worst_entry'] = None
		aggregate_results[permutation] = permutation_stats

	permutation_stats = aggregate_results[permutation]
	permutation_stats['total_raw_size'] += clip_stats['acl_raw_size']
	permutation_stats['total_compressed_size'] += run_stats['compressed_size']
	permutation_stats['total_compression_time'] += run_stats['compression_time']
	permutation_stats['acl_max_error'] = max(run_stats['acl_max_error'], permutation_stats['acl_max_error'])
	permutation_stats['ue4_max_error'] = max(run_stats['ue4_max_error'], permutation_stats['ue4_max_error'])
	permutation_stats['num_runs'] += 1
	if run_stats['acl_max_error'] > permutation_stats['worst_error']:
		permutation_stats['worst_error'] = run_stats['acl_max_error']
		permutation_stats['worst_entry'] = clip_stats

def do_parse_stats(options, stat_queue, result_queue):
	try:
		stats = []
		acl_error_values = []
		ue4_error_values = []
		ue4_keyreduction_data = {}
		ue4_keyreduction_data['drop_rates'] = []
		ue4_keyreduction_data['pose_drop_rates'] = []
		ue4_keyreduction_data['track_drop_rates'] = []
		acl_compression_times = []
		ue4_compression_times = []

		while True:
			stat_filename = stat_queue.get()
			if stat_filename is None:
				break

			if platform.system() == 'Windows':
				filename = '\\\\?\\{}'.format(stat_filename) # Long path prefix
			else:
				filename = stat_filename

			with open(filename, 'r') as file:
				try:
					file_data = sjson.loads(file.read())
					if 'error' in file_data:
						print('{} [{}]'.format(file_data['error'], stat_filename))
						continue

					file_data['filename'] = stat_filename
					file_data['clip_name'] = os.path.splitext(os.path.basename(stat_filename))[0].replace('_stats', '')

					if not options['csv_error']:
						# The sjson lib doesn't always return numbers as floats, sometimes as int but numpy doesn't like that
						if 'ue4_acl' in file_data and 'error_per_frame_and_bone' in file_data['ue4_acl']:
							for frame_error_values in file_data['ue4_acl']['error_per_frame_and_bone']:
								acl_error_values.extend([float(v) for v in frame_error_values])
							file_data['ue4_acl']['error_per_frame_and_bone'] = []

						if 'ue4_auto' in file_data and 'error_per_frame_and_bone' in file_data['ue4_auto']:
							for frame_error_values in file_data['ue4_auto']['error_per_frame_and_bone']:
								ue4_error_values.extend([float(v) for v in frame_error_values])
							file_data['ue4_auto']['error_per_frame_and_bone'] = []

					if 'ue4_acl' in file_data:
						acl_compression_times.append(file_data['ue4_acl']['compression_time'])

					if 'ue4_auto' in file_data:
						ue4_compression_times.append(file_data['ue4_auto']['compression_time'])

					if 'ue4_keyreduction' in file_data:
						num_animated_keys = float(file_data['ue4_keyreduction']['total_num_animated_keys'])
						if num_animated_keys > 2.001:
							drop_rate = float(file_data['ue4_keyreduction']['total_num_dropped_animated_keys']) / num_animated_keys
							ue4_keyreduction_data['drop_rates'].append(drop_rate)
							ue4_keyreduction_data['pose_drop_rates'].extend([float(v) for v in file_data['ue4_keyreduction']['dropped_pose_keys']])
							ue4_keyreduction_data['track_drop_rates'].extend([float(v) for v in file_data['ue4_keyreduction']['dropped_track_keys']])
						else:
							ue4_keyreduction_data['drop_rates'].append(0.0)
							ue4_keyreduction_data['pose_drop_rates'].extend([0.0 for v in file_data['ue4_keyreduction']['dropped_pose_keys']])
							ue4_keyreduction_data['track_drop_rates'].extend([0.0 for v in file_data['ue4_keyreduction']['dropped_track_keys']])

					stats.append(file_data)
				except sjson.ParseException:
					print('Failed to parse SJSON file: {}'.format(stat_filename))

			result_queue.put(('progress', stat_filename))

		results = {}
		results['stats'] = stats
		results['acl_error_values'] = acl_error_values
		results['ue4_error_values'] = ue4_error_values
		results['ue4_keyreduction_data'] = ue4_keyreduction_data
		results['acl_compression_times'] = acl_compression_times
		results['ue4_compression_times'] = ue4_compression_times

		result_queue.put(('done', results))
	except KeyboardInterrupt:
		print('Interrupted')

def parallel_parse_stats(options, stat_files, label):
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

	if options['dual_stat_inputs']:
		label = ' {}'.format(label)
	else:
		label = ''	# No need for a label if we parse both together

	num_stat_files = len(stat_files)
	num_stat_file_processed = 0
	stats = []
	print_progress(num_stat_file_processed, len(stat_files), 'Aggregating{} results:'.format(label), '{} / {}'.format(num_stat_file_processed, num_stat_files))
	try:
		while True:
			try:
				(msg, data) = result_queue.get(True, 1.0)
				if msg == 'progress':
					num_stat_file_processed += 1
					print_progress(num_stat_file_processed, len(stat_files), 'Aggregating{} results:'.format(label), '{} / {}'.format(num_stat_file_processed, num_stat_files))
				elif msg == 'done':
					stats.append(data)
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

def get_stat_files(options):
	if options['dual_stat_inputs']:
		acl_stat_files = []
		ue4_stat_files = []

		for (dirpath, dirnames, filenames) in os.walk(options['acl_stats']):
			for filename in filenames:
				if not filename.endswith('.sjson'):
					continue

				stat_filename = os.path.join(dirpath, filename)
				acl_stat_files.append(stat_filename)

		for (dirpath, dirnames, filenames) in os.walk(options['ue4_stats']):
			for filename in filenames:
				if not filename.endswith('.sjson'):
					continue

				stat_filename = os.path.join(dirpath, filename)
				ue4_stat_files.append(stat_filename)

		acl_file_set = set([os.path.basename(file) for file in acl_stat_files])
		ue4_file_set = set([os.path.basename(file) for file in ue4_stat_files])
		if len(acl_file_set.intersection(ue4_file_set)) != len(acl_stat_files):
			print('The input files for ACL and UE4 do not match, some are missing in one or the other')
			sys.exit(1)

		return (acl_stat_files, ue4_stat_files)
	else:
		stat_files = []

		for (dirpath, dirnames, filenames) in os.walk(options['stats']):
			for filename in filenames:
				if not filename.endswith('.sjson'):
					continue

				stat_filename = os.path.join(dirpath, filename)
				stat_files.append(stat_filename)

		return (stat_files, stat_files)

def percentile_rank(values, value):
	return (values < value).mean() * 100.0

if __name__ == "__main__":
	options = parse_argv()

	acl_stat_files, ue4_stat_files = get_stat_files(options)

	if len(acl_stat_files) == 0:
		print('No input clips found')
		sys.exit(0)

	aggregating_start_time = time.clock()

	acl_stats = parallel_parse_stats(options, acl_stat_files, 'ACL')
	if options['dual_stat_inputs']:
		ue4_stats = parallel_parse_stats(options, ue4_stat_files, 'UE4 Auto')
	else:
		ue4_stats = acl_stats

	acl_error_values = numpy.array([])
	acl_compression_times = numpy.array([])
	for result in acl_stats:
		acl_error_values = numpy.append(acl_error_values, result['acl_error_values'])
		acl_compression_times = numpy.append(acl_compression_times, result['acl_compression_times'])

	ue4_error_values = numpy.array([])
	ue4_compression_times = numpy.array([])
	for result in ue4_stats:
		ue4_error_values = numpy.append(ue4_error_values, result['ue4_error_values'])
		ue4_compression_times = numpy.append(ue4_compression_times, result['ue4_compression_times'])

	clip_drop_rates = numpy.array([])
	pose_drop_rates = numpy.array([])
	track_drop_rates = numpy.array([])
	for result in acl_stats:
		clip_drop_rates = numpy.append(clip_drop_rates, result['ue4_keyreduction_data']['drop_rates'])
		pose_drop_rates = numpy.append(pose_drop_rates, result['ue4_keyreduction_data']['pose_drop_rates'])
		track_drop_rates = numpy.append(track_drop_rates, result['ue4_keyreduction_data']['track_drop_rates'])
	clip_drop_rates = numpy.sort(clip_drop_rates)
	pose_drop_rates = numpy.sort(pose_drop_rates)
	track_drop_rates = numpy.sort(track_drop_rates)

	# Flatten our stats into a list and strip the error values
	acl_stats = [ stat for result in acl_stats for stat in result['stats'] ]
	ue4_stats = [ stat for result in ue4_stats for stat in result['stats'] ]

	# Sort out stats by clip name so we can zip them in pairs
	acl_stats.sort(key=lambda stat: stat['clip_name'])
	ue4_stats.sort(key=lambda stat: stat['clip_name'])

	merged_stats = list(zip(acl_stats, ue4_stats))

	aggregating_end_time = time.clock()
	print('Parsed stats in {}'.format(format_elapsed_time(aggregating_end_time - aggregating_start_time)))

	if options['csv_summary']:
		output_csv_summary(os.getcwd(), merged_stats)

	if options['csv_error']:
		output_csv_error(os.getcwd(), merged_stats)

	if options['csv_kr'] and len(clip_drop_rates) > 0:
		output_csv_kr(os.getcwd(), clip_drop_rates, pose_drop_rates, track_drop_rates)

	print()
	print('Stats per run type:')
	aggregate_results = {}
	num_acl_size_wins = 0
	num_acl_accuracy_wins = 0
	num_acl_speed_wins = 0
	num_acl_wins = 0
	num_acl_auto_wins = 0
	for (stat_acl, stat_auto) in merged_stats:
		if 'ue4_auto' in stat_auto:
			ue4_auto = stat_auto['ue4_auto']
			ue4_auto['desc'] = '{} {} {}'.format(ue4_auto['algorithm_name'], ue4_auto['rotation_format'], ue4_auto['translation_format'])
			append_stats('ue4_auto', stat_auto, ue4_auto, aggregate_results)

		if 'ue4_acl' in stat_acl:
			ue4_acl = stat_acl['ue4_acl']
			ue4_acl['desc'] = ue4_acl['algorithm_name']
			append_stats('ue4_acl', stat_acl, ue4_acl, aggregate_results)

		if 'ue4_keyreduction' in stat_acl:
			ue4_keyreduction = stat_acl['ue4_keyreduction']
			ue4_keyreduction['desc'] = ue4_keyreduction['algorithm_name']
			append_stats('ue4_keyreduction', stat_acl, ue4_keyreduction, aggregate_results)

		if 'ue4_auto' in stat_auto and 'ue4_acl' in stat_acl:
			ue4_auto = stat_auto['ue4_auto']
			ue4_acl = stat_acl['ue4_acl']
			if ue4_acl['compressed_size'] < ue4_auto['compressed_size']:
				num_acl_size_wins += 1
			if ue4_acl['ue4_max_error'] < ue4_auto['ue4_max_error']:
				num_acl_accuracy_wins += 1
			if ue4_acl['compression_time'] < ue4_auto['compression_time']:
				num_acl_speed_wins += 1
			if ue4_acl['compressed_size'] < ue4_auto['compressed_size'] and ue4_acl['ue4_max_error'] < ue4_auto['ue4_max_error'] and ue4_acl['compression_time'] < ue4_auto['compression_time']:
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
		print('Compressed {:.2f} MB, Elapsed {}, Ratio [{:.2f} : 1], Max error [UE4: {:.4f}, ACL: {:.4f}]'.format(bytes_to_mb(ue4_auto['total_compressed_size']), format_elapsed_time(ue4_auto['total_compression_time']), ratio, ue4_auto['ue4_max_error'], ue4_auto['acl_max_error']))
		print('Least accurate: {} Ratio: {:.2f}, Error: {:.4f}'.format(ue4_auto['worst_entry']['clip_name'], ue4_auto['worst_entry']['ue4_auto']['acl_compression_ratio'], ue4_auto['worst_entry']['ue4_auto']['acl_max_error']))
		print('Compression speed: {:.2f} KB/sec'.format(bytes_to_kb(raw_size) / ue4_auto['total_compression_time']))
		print('Compression time 50, 85, 99th percentile: {:.3f}, {:.3f}, {:.3f} seconds'.format(numpy.percentile(ue4_compression_times, 50.0), numpy.percentile(ue4_compression_times, 85.0), numpy.percentile(ue4_compression_times, 99.0)))
		if len(ue4_error_values) > 0:
			print('Bone error 99th percentile: {:.4f}'.format(numpy.percentile(ue4_error_values, 99.0)))
			print('Error threshold percentile rank: {:.2f} (0.01)'.format(percentile_rank(ue4_error_values, 0.01)))
		print()

	if 'ue4_acl' in aggregate_results:
		ue4_acl = aggregate_results['ue4_acl']
		raw_size = ue4_acl['total_raw_size']
		ratio = float(ue4_acl['total_raw_size']) / float(ue4_acl['total_compressed_size'])
		print('Total ACL Compression:')
		print('Compressed {:.2f} MB, Elapsed {}, Ratio [{:.2f} : 1], Max error [UE4: {:.4f}, ACL: {:.4f}]'.format(bytes_to_mb(ue4_acl['total_compressed_size']), format_elapsed_time(ue4_acl['total_compression_time']), ratio, ue4_acl['ue4_max_error'], ue4_acl['acl_max_error']))
		print('Least accurate: {} Ratio: {:.2f}, Error: {:.4f}'.format(ue4_acl['worst_entry']['clip_name'], ue4_acl['worst_entry']['ue4_acl']['acl_compression_ratio'], ue4_acl['worst_entry']['ue4_acl']['acl_max_error']))
		print('Compression speed: {:.2f} KB/sec'.format(bytes_to_kb(raw_size) / ue4_acl['total_compression_time']))
		print('Compression time 50, 85, 99th percentile: {:.3f}, {:.3f}, {:.3f} seconds'.format(numpy.percentile(acl_compression_times, 50.0), numpy.percentile(acl_compression_times, 85.0), numpy.percentile(acl_compression_times, 99.0)))
		if len(acl_error_values) > 0:
			print('Bone error 99th percentile: {:.4f}'.format(numpy.percentile(acl_error_values, 99.0)))
			print('Error threshold percentile rank: {:.2f} (0.01)'.format(percentile_rank(acl_error_values, 0.01)))
		print()

	if 'ue4_keyreduction' in aggregate_results:
		ue4_keyreduction = aggregate_results['ue4_keyreduction']
		raw_size = ue4_keyreduction['total_raw_size']
		ratio = float(ue4_keyreduction['total_raw_size']) / float(ue4_keyreduction['total_compressed_size'])
		print('Total Key Reduction Compression:')
		print('Compressed {:.2f} MB, Elapsed {}, Ratio [{:.2f} : 1], Max error [UE4: {:.4f}, ACL: {:.4f}]'.format(bytes_to_mb(ue4_keyreduction['total_compressed_size']), format_elapsed_time(ue4_keyreduction['total_compression_time']), ratio, ue4_keyreduction['ue4_max_error'], ue4_keyreduction['acl_max_error']))
		print('Least accurate: {} Ratio: {:.2f}, Error: {:.4f}'.format(ue4_keyreduction['worst_entry']['clip_name'], ue4_keyreduction['worst_entry']['ue4_keyreduction']['acl_compression_ratio'], ue4_keyreduction['worst_entry']['ue4_keyreduction']['acl_max_error']))
		print('Compression speed: {:.2f} KB/sec'.format(bytes_to_kb(raw_size) / ue4_keyreduction['total_compression_time']))
		#print('Bone error 99th percentile: {:.4f}'.format(numpy.percentile(acl_error_values, 99.0)))
		#print('Error threshold percentile rank: {:.2f} (0.01)'.format(percentile_rank(acl_error_values, 0.01)))
		print()

	num_clips = float(len(acl_stat_files))
	print('Raw size: {:.2f} MB'.format(bytes_to_mb(raw_size)))
	print('ACL was smaller for {} clips ({:.2f} %)'.format(num_acl_size_wins, float(num_acl_size_wins) / num_clips * 100.0))
	print('ACL was more accurate for {} clips ({:.2f} %)'.format(num_acl_accuracy_wins, float(num_acl_accuracy_wins) / num_clips * 100.0))
	print('ACL has faster compression for {} clips ({:.2f} %)'.format(num_acl_speed_wins, float(num_acl_speed_wins) / num_clips * 100.0))
	print('ACL was smaller, better, faster for {} clips ({:.2f} %)'.format(num_acl_wins, float(num_acl_wins) / num_clips * 100.0))
	print('ACL won with simulated auto {} clips ({:.2f} %)'.format(num_acl_auto_wins, float(num_acl_auto_wins) / num_clips * 100.0))

	if len(clip_drop_rates) > 0:
		print()
		print('Key reduction clip avg drop rate: {:.2f} %'.format(numpy.average(clip_drop_rates) * 100.0))
		print('Key reduction clip 50th percentile drop rate: {:.2f} %'.format(numpy.percentile(clip_drop_rates, 50.0) * 100.0))
		print('Key reduction clip 90th percentile drop rate: {:.2f} %'.format(numpy.percentile(clip_drop_rates, 90.0) * 100.0))
		print('Key reduction pose avg drop rate: {:.2f} %'.format(numpy.average(pose_drop_rates) * 100.0))
		print('Key reduction pose 50th percentile drop rate: {:.2f} %'.format(numpy.percentile(pose_drop_rates, 50.0) * 100.0))
		print('Key reduction pose 90th percentile drop rate: {:.2f} %'.format(numpy.percentile(pose_drop_rates, 90.0) * 100.0))
		print('Key reduction track avg drop rate: {:.2f} %'.format(numpy.average(track_drop_rates) * 100.0))
		print('Key reduction track 50th percentile drop rate: {:.2f} %'.format(numpy.percentile(track_drop_rates, 50.0) * 100.0))
		print('Key reduction track 90th percentile drop rate: {:.2f} %'.format(numpy.percentile(track_drop_rates, 90.0) * 100.0))
