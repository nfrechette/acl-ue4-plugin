## Engine integration into UE 4.24 and earlier

In order to use the ACL plugin in *Unreal Engine 4.24* and earlier, you will need to manually integrate a few engine changes. These changes can be found in the following *GitHub* branches:

*  **4.19.x:** [branch](https://github.com/nfrechette/UnrealEngine/tree/4.19-acl) - [patch](https://github.com/nfrechette/UnrealEngine/pull/3.patch) (requires ACL plugin **v0.3 or earlier**)
*  4.20.x: Use 4.19.x for inspiration, few to no engine changes should conflict (requires ACL plugin **v0.3 or earlier**)
*  4.21.x: Use 4.22.x for inspiration, few to no engine changes should conflict (requires ACL plugin **v0.4**)
*  **4.22.x:** [branch](https://github.com/nfrechette/UnrealEngine/tree/4.22-acl) - [patch](https://github.com/nfrechette/UnrealEngine/pull/4.patch) (requires ACL plugin **v0.4**)
*  **4.23.x:** [branch](https://github.com/nfrechette/UnrealEngine/tree/4.23-acl) - [patch](https://github.com/nfrechette/UnrealEngine/pull/5.patch) (requires ACL plugin **v0.5**)
*  **4.24.x:** [branch](https://github.com/nfrechette/UnrealEngine/tree/4.24-acl) - [patch](https://github.com/nfrechette/UnrealEngine/pull/6.patch) (requires ACL plugin **v0.6**)

Note that in order to see the custom engine branches linked above, you will first need to [request access](https://www.unrealengine.com/en-US/ue4-on-github) to the *Unreal Engine* source code.

Some engine changes are required for the ACL plugin to work with older Unreal Engine versions. The changes are minimal and consist of a global registry for animation codecs that plugins can hook into as well as exposing a few things needed. The branches in my fork of the Unreal Engine do not contain the ACL plugin. You will have to download a plugin release suitable for your engine version. Simply place the `ACLPlugin` directory under `<UE4 Root>\Engine\Plugins` or in the plugin directory of your project.
