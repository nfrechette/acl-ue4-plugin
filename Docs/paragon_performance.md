# Paragon database performance

|                   | ACL Plugin v0.4.0 | ACL Plugin v0.3.0 | UE v4.22.2   |
| -------               | --------      | -------               | -------               |
| **Compressed size**   | 234.76 MB | 234.76 MB | 380.37 MB |
| **Compression ratio** | 18.22 : 1 | 18.22 : 1 | 11.24 : 1   |
| **Compression time**  | 23m 58.76s | 30m 14.69s | 2h 5m 11.22s |
| **Compression speed** | 3043.42 KB/sec | 2412.94 KB/sec | 582.96 KB/sec |
| **Max ACL error**     | 0.8623 cm | 0.8623 cm | 0.8619 cm      |
| **Max UE4 error**     | 0.8601 cm | 0.8601 cm | 0.6424 cm      |
| **ACL Error 99<sup>th</sup> percentile** | 0.0100 cm | 0.0094 cm | 0.0438 cm |
| **Samples below ACL error threshold** | 99.00 % | 99.19 % | 81.75 % |

ACL was smaller for **6343** clips (**96.72 %**)  
ACL was more accurate for **5081** clips (**77.48 %**)  
ACL has faster compression for **6514** clips (**99.33 %**)  
ACL was smaller, better, and faster for **4911** clips (**74.89 %**)  

Would the *ACL Plugin* have been included in the *Automatic Compression* permutations tried, it would have won for **6181** clips (**94.25 %**)

## Data and method used

To compile these statistics, a large number of animations from [Paragon](https://www.epicgames.com/paragon) are used.
In October 2017 the animations were manually extracted and converted to the [ACL file format](https://github.com/nfrechette/acl/blob/develop/docs/the_acl_file_format.md) losslessly. The data is sadly **NOT** available upon request.
Epic has permitted [Nicholas Frechette](https://github.com/nfrechette) to use them for research purposes only under a non-disclosure agreement.

**Note: Epic has since released Paragon assets publicly in early 2018, once I get around to it, it will be extracted along with updated stats.**

*  Number of clips: **6558**
*  Total duration: **7h 00m 45.27s**
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