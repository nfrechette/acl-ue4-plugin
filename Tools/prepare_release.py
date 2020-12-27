import os
import shutil
import sys

if __name__ == "__main__":
	if len(sys.argv) != 2:
		print('Invalid usage: python {}'.format(' '.join(sys.argv)))
		print('Usage: python prepare_release.py <version> (e.g. 4.25)')
		sys.exit(1)

	target_ue4_version = sys.argv[1]

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
	rtm_root_dir = os.path.join(acl_root_dir, 'external', 'rtm')
	sjsoncpp_root_dir = os.path.join(acl_root_dir, 'external', 'sjson-cpp')

	shutil.rmtree(os.path.join(acl_root_dir, 'external', 'catch2'))
	shutil.rmtree(os.path.join(rtm_root_dir, 'external', 'catch2'))
	shutil.rmtree(os.path.join(sjsoncpp_root_dir, 'external', 'catch2'))

	shutil.rmtree(os.path.join(acl_root_dir, 'external', 'benchmark'))
	shutil.rmtree(os.path.join(rtm_root_dir, 'external', 'benchmark'))

	print('Setting uplugin version to: {} ...'.format(target_ue4_version))
	uplugin_file = os.path.join(plugin_dst_dir, 'ACLPlugin.uplugin')
	with open(uplugin_file) as f:
		uplugin_file_content = f.read()
	uplugin_file_content = uplugin_file_content.replace('4.25.0', target_ue4_version + '.0')
	with open(uplugin_file, 'w') as f:
		f.write(uplugin_file_content)

	print('Zipping ACLPlugin ...')
	zip_filename = os.path.join(root_dir, 'ACLPlugin_' + target_ue4_version)
	shutil.make_archive(zip_filename, 'zip', staging_dir)

	print('Done!')
	sys.exit(0)
