import json
import os
import shutil
import sys

if __name__ == "__main__":
	if len(sys.argv) != 2:
		print('Invalid usage: python {}'.format(' '.join(sys.argv)))
		print('Usage: python prepare_release.py <version> (e.g. 4.25)')
		sys.exit(1)

	target_ue_version = sys.argv[1]
	target_ue_versions = target_ue_version.split('.')
	target_ue_version_major = int(target_ue_versions[0])
	target_ue_version_minor = int(target_ue_versions[1])

	tools_dir = os.path.dirname(os.path.realpath(__file__))
	root_dir = os.path.join(tools_dir, '..')
	staging_dir = os.path.join(root_dir, 'Staging')

	if os.path.exists(staging_dir):
		print('Cleaning previous staging environment ...')
		shutil.rmtree(staging_dir)

	if not os.path.exists(staging_dir):
		os.makedirs(staging_dir)

	plugin_version = None
	with open(os.path.join(root_dir, 'ACLPLugin', 'ACLPlugin.uplugin'), 'r') as f:
		data = json.load(f)
		if not 'VersionName' in data:
			print('UE Plugin version not found in ACLPlugin.uplugin')
			sys.exit(1)
		plugin_version = data['VersionName']

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

	print('Pre-processing classes ...')

	# Base mappings
	pre_process_mappings = {
		'ACL_IMPL_ANIM_BONE_COMPRESSION_CODEC_PTR': 'class UAnimBoneCompressionCodec*',
		'ACL_IMPL_ANIM_SEQUENCE_PTR': 'class UAnimSequence*',
		'ACL_IMPL_ANIMATION_COMPRESSION_LIBRARY_DATABASE_PTR': 'class UAnimationCompressionLibraryDatabase*',
		'ACL_IMPL_SKELETAL_MESH_PTR': 'class USkeletalMesh*',
	}

	# Overrides
	if target_ue_version_major >= 5:
		if target_ue_version_minor >= 1:
			pre_process_mappings.update({
				'ACL_IMPL_ANIM_BONE_COMPRESSION_CODEC_PTR': 'TObjectPtr<class UAnimBoneCompressionCodec>',
				'ACL_IMPL_ANIM_SEQUENCE_PTR': 'TObjectPtr<class UAnimSequence>',
				'ACL_IMPL_ANIMATION_COMPRESSION_LIBRARY_DATABASE_PTR': 'TObjectPtr<class UAnimationCompressionLibraryDatabase>',
				'ACL_IMPL_SKELETAL_MESH_PTR': 'TObjectPtr<class USkeletalMesh>',
			})

	classes_dir = os.path.join(plugin_dst_dir, 'Source', 'ACLPlugin', 'Classes')
	for (dirpath, dirnames, filenames) in os.walk(classes_dir):
		for filename in filenames:
			if filename.endswith('.h.in'):
				src_filename = os.path.join(dirpath, filename)
				with open(src_filename) as f:
					src_file_content = f.read()

				for k, v in pre_process_mappings.items():
					src_file_content = src_file_content.replace('%{}%'.format(k), v)

				dst_filename = src_filename.replace('.h.in', '.h')
				with open(dst_filename, 'w') as f:
					f.write(src_file_content)

				os.remove(src_filename)

	print('Setting uplugin version to: {} ...'.format(target_ue_version))
	uplugin_file = os.path.join(plugin_dst_dir, 'ACLPlugin.uplugin')
	with open(uplugin_file) as f:
		uplugin_file_content = f.read()
	uplugin_file_content = uplugin_file_content.replace('4.25.0', target_ue_version + '.0')
	with open(uplugin_file, 'w') as f:
		f.write(uplugin_file_content)

	print('Zipping ACLPlugin ...')
	zip_filename = os.path.join(root_dir, 'ACLPlugin_v' + plugin_version + '_' + target_ue_version)
	shutil.make_archive(zip_filename, 'zip', staging_dir)

	print('Done!')
	sys.exit(0)
