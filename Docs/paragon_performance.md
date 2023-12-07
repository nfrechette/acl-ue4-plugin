# Paragon database performance

|                   | ACL Plugin v2.1.0 | ACL Plugin v2.0.0 | UE v5.2.0 |
| -------               | --------      | -------               | -------               |
| **Compressed size**   | 183.98 MB | 224.30 MB | 391.66 MB |
| **Compression ratio** | 23.24 : 1 | 19.06 : 1 | 10.92 : 1 |
| **Compression time**  | 19m 22.95s | 8m 32.77s | 33m 24.28s |
| **Compression speed** | 3765.21 KB/sec | 8539.37 KB/sec | 2184.70 KB/sec |
| **Max ACL error**     | 1.2065 cm | 0.8622 cm | 1.2169 cm |
| **Max UE error**     | 0.8434 cm | 0.8602 cm | 0.8491 cm |
| **ACL Error 99<sup>th</sup> percentile** | 0.0197 cm | 0.0095 cm | 0.0358 cm |
| **Samples below ACL error threshold** | 94.78 % | 99.13 % | 82.81 % |

ACL was smaller for **6469** clips (**98.64 %**)  
ACL was more accurate for **4829** clips (**73.64 %**)  
ACL has faster compression for **5302** clips (**80.85 %**)  
ACL was smaller, better, and faster for **3896** clips (**59.41 %**)  

Would the *ACL Plugin* have been included in the *Automatic Compression* permutations tried, it would have won for **6479** clips (**98.80 %**)

**Note: Numbers for ACL 2.0 were extracted with UE 4.25.**

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

To measure and extract the compression statistics, the provided [commandlet](../ACLPlugin/Source/ACLPluginEditor/Classes/ACLStatsDumpCommandlet.h) is used along with a [python script](../ACLPlugin/Extras/stat_parser.py) to parse the results.

The *ACL Plugin* uses the default settings with an error threshold of **0.01cm** while *UE* uses the *Automatic Compression* with an error threshold (master tolerance) of **0.1 cm**. Both error thresholds used are suitable for production use. The **99th** percentile and the number of samples below the ACL error threshold are calculated by measuring the error with ACL on every bone at every sample.

*ACL* and *UE* both use separate methods to measure the error and both values are shown for transparency.
