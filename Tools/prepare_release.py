import os
import shutil
import sys

if __name__ == "__main__":
	tools_dir = os.path.dirname(os.path.realpath(__file__))
	root_dir = os.path.join(tools_dir, '..')
	staging_dir = os.path.join(root_dir, 'Staging')

	if os.path.exists(staging_dir):
		print('Cleaning previous staging environment ...')
		shutil.rmtree(staging_dir)

	if not os.path.exists(staging_dir):
		os.makedirs(staging_dir)

	print('Copying plugin content ...')
	plugin_src_dir = os.path.join(root_dir, 'ACLPlugin')
	plugin_dst_dir = os.path.join(staging_dir, 'ACLPlugin')
	shutil.copytree(plugin_src_dir, plugin_dst_dir)

	# Remove catch2 and other third party dependencies we do not own or need
	print('Removing what we don\'t need ...')
	acl_root_dir = os.path.join(plugin_dst_dir, 'Source', 'ThirdParty', 'acl')
	shutil.rmtree(os.path.join(acl_root_dir, 'external', 'catch2'))

	rtm_root_dir = os.path.join(acl_root_dir, 'external', 'rtm')
	shutil.rmtree(os.path.join(rtm_root_dir, 'external', 'catch2'))

	rtm_benchmark_dir = os.path.join(rtm_root_dir, 'external', 'benchmark')
	if os.path.exists(rtm_benchmark_dir):
		print('NEED TO REMOVE RTM BENCHMARK')	# Will be added in RTM 2.0, make sure we don't forget it
		sys.exit(1)

	sjsoncpp_root_dir = os.path.join(acl_root_dir, 'external', 'sjson-cpp')
	shutil.rmtree(os.path.join(sjsoncpp_root_dir, 'external', 'catch2'))

	print('Zipping ACLPlugin ...')
	zip_filename = os.path.join(root_dir, 'ACLPlugin')
	shutil.make_archive(zip_filename, 'zip', staging_dir)

	print('Done!')
	sys.exit(0)
