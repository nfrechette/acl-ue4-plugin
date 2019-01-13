# Error measurements

Both UE4 and ACL measure the animation compression error differently. Although both approaches are very similar and mostly agree for the vast majority of animation sequences, some exotic sequences can end up with a very large disagreement between the two. Subtle differences in both approaches can compound to yield very different measurements. Three differences stand out: how the error is measured, where the error is measured in space, and where the error is measured in time.

## How the error is measured

While both UE4 and ACL use [virtual vertices](http://nfrechette.github.io/2016/11/01/anim_compression_accuracy/) in order to approximate the visual mesh, how they do so differs slightly: ACL uses **three** virtual vertices on *every* bone and UE4 uses a **single** virtual vertex on *leaf* bones. Due to the hierarchical nature of skeleton based animation data, testing leaf bones is a pretty good approximation but ACL goes one step further and tests every bone regardless. It does this because each bone might have its virtual vertices at different distances. In this respect, ACL is more conservative.

## Where the error is measured in space

As previously mentioned, ACL uses **three** virtual vertices while UE4 uses just **one**. UE4 constructs this vertex with all three vector components equal to one of two values: 1cm or 100cm. Which value is used depends on whether the bone is in the list of end effectors: bones in the list use a higher value while those that do not use the lower value. Because all three components are equal, the virtual vertex distance is actually **?cm**.

If our bone transform is made entirely of translation then that is sufficient. However, most bones will end up with some amount of rotation either directly animated or as a result of a parent bone contribution. As the rotation axis approaches our virtual vertex, the measured error will decrease in accuracy to the point where if they are colinear, the error contribution from the rotation will become entirely invisible: a point that lies on a rotation axis is never transformed by that rotation. In a worst case scenario, the rotation error could be infinite and we wouldn't be able to tell. For that reason, a single virtual vertex isn't sufficient.

If our bone transform is made entirely of translation and rotation then using two perpendicular virtual vertices is good enough: even if one is entirely colinear with our rotation axis, the other will be fully perpendicular and register the error properly. At worst, if the rotation axis lies in between, neither will be too close to lose precision. To measure our error, we transform both vertices and measure their respective error in order to retain the maximum value or the two.

However, once 3D scale enters the picture, two virtual vertices are no longer sufficient. As our scale approaches zero along an axis, the error it contributes will diminish to the point where it vanishes entirely at zero. Our two vertices form a plane. If one axis collapses to zero, that plane now folds into a line. Once our second axis collapses to zero, our line now becomes a point at zero. This makes the error contributed by our third axis entirely invisible. In order to avoid this issue, ACL uses a third perpendicular virtual vertex: as long as one axis has non-zero scale, its error will be visible.

The ACL virtual vertices use slightly different values to determine the accuracy. They either have a distance of **?cm** for end effector bones or a distance of **?cm** for ordinary bones.

**TODO: Add missing measurements and note about conservativeness**

## Where the error is measured in time

UE4 stores its raw animation sequence data as uniform samples. A fixed sample rate dictates where each sample lies in time. While some of the UE4 codecs perform some form of sample (or key) reduction when they can be linearly interpolated from their neighbors, ACL retains every sample. Both UE4 and ACL use linear interpolation when sampling in between existing samples. As a result, it is sufficient to measure the error on the original raw samples in order to accurately measure the error: only two samples ever contribute to our final value and they both contribute proportionally.

To do so, UE4 iterates over every sample and it calculates the sample time from the index and the sampling rate. This is done because it does not support sampling an individual sample directly in compressed form. However, due to floating point rounding we might end up with a small interpolation factor. More often than not this is good enough an approximation but in rare cases where the samples we interpolate are very far from one another, the reconstructed sample might lie quite far from the one we really wanted. Codecs that remove samples further complicate things because the interpolation factor calculated might differ slightly in raw and compressed form even if both samples around the one we want are retained. Last but not least, compiler optimizations such as *fast math* can also alter how the interpolation alpha is calculated in different pieces of code which can lead them to measure the error differently at the same sample time.

In order to have the most accurate view possible of the error, ACL does not perform any form of interpolation either in the raw or compressed samples. This is easily achieved because its sampling function supports a rounding mode: floor (sample that immediately precedes our sample time), ceil (sample that immediately follows our sample time), nearest (closest sample to our sample time), and none (linear interpolation between our two samples). The error is always measured with the nearest sample to the sample time we care about.

By removing interpolation from the picture, ACL ensures a consistent and accurate view of the compression error.

## Who to trust?

If in doubt, it is best to trust the ACL error measurement. It is more accurate and very conservative. Unfortunately it isn't visible in the editor UI when compression is performed. In order to see it when compressing an animation sequence, simply switch to **verbose** the logging category for animation compression: `log AnimationCompression verbose` (in the console). If the error reported by ACL is unusually high, then it is possible that a bug was found and you are encouraged to log an issue and/or reach out. Note that the first thing I will ask if you report accuracy issues is what is the ACL reported error.

**TODO: Double check log verbosity command line**
