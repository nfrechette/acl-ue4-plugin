# Significant changes per release

## 2.0.3

*  Fix potential crash with AVX2 due to incorrect allocation alignment
*  Upgraded to ACL v2.0.1

## 2.0.2

*  Fix assert when running memreport
*  Fix memreport reported usage for ACL
*  Fix crash when streaming in multiple database chunks

## 2.0.1

*  Add missing include for marketplace submission

## 2.0.0

*  Upgraded to ACL v2.0.0
*  Added support for streaming databases (code and blueprints)
*  Added ability to specify a skeletal mesh to improve accuracy
*  Fixed all MSVC static analysis warnings/issues
*  Other minor improvements and clean up

## 1.0.6

*  Add support for UE 4.26

## 1.0.5

*  Upgraded to ACL v1.3.5
*  Fix crash when more than 50000 frames are present by failing gracefully

## 1.0.4

*  Upgraded to ACL v1.3.4
*  Fix harmless assert when compressing a single frame additive sequence

## 1.0.3

*  Upgraded to ACL v1.3.3
*  Fix build when logging is disabled in non-shipping builds

## 1.0.2

*  Changes required to meet the UE4 marketplace guidelines
*  Added console commands to display animation codec usage statistics
*  Minor fixes

## 1.0.1

*  Fix crash when using the curve compression codec and no curves are present
*  Added ACL curve compression settings asset

## 1.0.0

*  Upgraded code to support UE 4.25 which can now be published on the Unreal Marketplace
*  Other minor changes to meet the Unreal Marketplace guidelines
*  Add support for AnimSequence curve compression
*  Upgraded to ACL v1.3.1

## 0.6.4

*  Upgraded to ACL v1.3.5
*  Fix crash when more than 50000 frames are present by failing gracefully

## 0.6.3

*  Upgraded to ACL v1.3.4
*  Fix harmless assert when compressing a single frame additive sequence

## 0.6.2

*  Upgraded to ACL v1.3.3

## 0.6.1

*  Fix crash when using the curve compression codec and no curves are present
*  Minor cleanup

## 0.6.0

*  Upgraded code to support UE 4.24
*  Add support for AnimSequence curve compression
*  Upgraded to ACL v1.3.1

## 0.5.0

*  Upgraded code to support UE 4.23
*  Upgraded to ACL v1.3.0
*  Added linear key reduction stat reporting
*  Other minor fixes and improvements

## 0.4.0

*  Upgraded code to support UE 4.22
*  Enable usage of popcount intrinsics
*  Other minor fixes and improvements

## 0.3.0

*  Upgraded to ACL v1.2.0
*  Added proper support for floating point sample rate
*  Added exposure to the new compression level
*  Invalidate the compressed data if the KeyEndEffectors array is changed
*  Other minor fixes and improvements

## 0.2.2

*  Fix additive animation sequences not being handled properly
*  Other minor fixes and changes

## 0.2.1

*  Fix linux cross-compilation issue

## 0.2.0

*  Upgrading to ACL v1.1.0 which offers very large decompression performance improvements
*  More decompression performance optimizations
*  The commandlet now allows you to dump stats from any local Unreal project

## 0.1.0

*  Initial release!
*  ACL v1.0.0 is fully supported as a drop in replacement for stock UE4 algorithms
*  Scripts and commandlets are provided for regression testing and metric extraction

