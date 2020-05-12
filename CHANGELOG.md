# Significant changes per release

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

