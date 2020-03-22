# Paragon database performance

|                   | ACL Plugin v1.0.0 | ACL Plugin v0.5.0 | UE v4.25.0 | UE v4.23.1 |
| -------               | --------      | -------               | -------               | -------               |
| **Compressed size**   | 225.39 MB | 225.42 MB | 384.99 MB | 382.95 MB |
| **Compression ratio** | 18.97 : 1 | 18.97 : 1 | 11.11 : 1 | 11.17 : 1 |
| **Compression time**  | 11m 2.79s | 11m 19.37s | 1h 14m 31.60s | 1h 35m 42.69s |
| **Compression speed** | 6606.51 KB/sec | 6445.27 KB/sec | 979.23 KB/sec | 762.49 KB/sec |
| **Max ACL error**     | 0.8622 cm | 0.8623 cm | 0.8619 cm | 0.8625 cm    |
| **Max UE4 error**     | 0.8601 cm | 0.8601 cm | 0.6454 cm | 0.8601 cm  |
| **ACL Error 99<sup>th</sup> percentile** | 0.0094 cm | 0.0094 cm | 0.0437 cm | 0.0445 cm |
| **Samples below ACL error threshold** | 99.20 % | 99.20 % | 81.47 % | 81.63 % |

ACL was smaller for **6368** clips (**97.10 %**)  
ACL was more accurate for **5056** clips (**77.10 %**)  
ACL has faster compression for **6533** clips (**99.62 %**)  
ACL was smaller, better, and faster for **4920** clips (**75.02 %**)  

Would the *ACL Plugin* have been included in the *Automatic Compression* permutations tried, it would have won for **6179** clips (**94.22 %**)

## Data and method used

To compile these statistics, a large number of animations from [Paragon](https://www.epicgames.com/paragon) are used.
In October 2017 the animations were manually extracted and converted to the [ACL file format](https://github.com/nfrechette/acl/blob/develop/docs/the_acl_file_format.md) losslessly. The data is sadly **NOT** available upon request.
Epic has permitted [Nicholas Frechette](https://github.com/nfrechette) to use them for research purposes only under a non-disclosure agreement.

**Note: Epic has since released Paragon assets publicly in early 2018, once I get around to it, it will be extracted along with updated stats.**

*  Number of clips: **6558**
*  Total duration: **7h 0m 45.27s**
*  Raw size: **4276.11 MB** (10x float32 * num bones * num samples)

The data set contains among other things:

*  Lots of characters with varying number of bones
*  Animated objects of various shape and form
*  Very short and very long clips
*  Clips with unusual sample rate (as low as **2** FPS!)
*  World space clips
*  Lots of 3D scale
*  Lots of other exotic clips

To measure and extract the compression statistics, the provided [commandlet](../ACLPlugin/Source/ACLPlugin/Classes/ACLStatsDumpCommandlet.h) is used along with a [python script](../Tools/stat_parser.py) to parse the results.

The *ACL Plugin* uses the default settings with an error threshold of **0.01cm** while *UE4* uses the *Automatic Compression* with an error threshold (master tolerance) of **0.1 cm**. Both error thresholds used are suitable for production use. The **99th** percentile and the number of samples below the ACL error threshold are calculated by measuring the error with ACL on every bone at every sample.

*ACL* and *UE4* both use separate methods to measure the error and both values are shown for transparency.