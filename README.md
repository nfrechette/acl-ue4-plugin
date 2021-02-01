[![CLA assistant](https://cla-assistant.io/readme/badge/nfrechette/acl-ue4-plugin)](https://cla-assistant.io/nfrechette/acl-ue4-plugin)
[![GitHub release](https://img.shields.io/github/release/nfrechette/acl-ue4-plugin.svg)](https://github.com/nfrechette/acl-ue4-plugin/releases)
[![GitHub license](https://img.shields.io/badge/license-MIT-blue.svg)](https://raw.githubusercontent.com/nfrechette/acl-ue4-plugin/master/LICENSE)

# Animation Compression Library Unreal Engine 4 Plugin

This plugin integrates the [Animation Compression Library](https://github.com/nfrechette/acl) version [v1.3.5](https://github.com/nfrechette/acl/releases/tag/v1.3.5) within [Unreal Engine 4](https://www.unrealengine.com/en-US/blog). It is suitable for every animation clip and platform as it features a low memory footprint, high accuracy, and very fast compression and decompression.

Compared to **UE 4.23.1**, the ACL plugin compresses up to **2.9x smaller**, is up to **4.7x more accurate**, up to **52.9x faster to compress**, and up to **6.8x faster to decompress** (results may vary depending on the platform and data).

The documentation on how to use it can be found [here](./Docs/README.md) along with performance results.

**NOTE: A refactor of the animation compression codecs within *Unreal Engine* is in the works and should be released soon. This will allow this plugin to be published on the Unreal Marketplace. Until then, it requires a few custom changes to the engine to work. The plugin code will be updated once the refactor becomes accessible to the public.**

## Getting started

If you would like to contribute to the ACL UE4 Plugin, make sure to check out the [contributing guidelines](CONTRIBUTING.md).

## License, copyright, and code of conduct

This project uses the [MIT license](LICENSE).

Copyright (c) 2018 Nicholas Frechette

Please note that this project is released with a [Contributor Code of Conduct](CODE_OF_CONDUCT.md). By participating in this project you agree to abide by its terms.
