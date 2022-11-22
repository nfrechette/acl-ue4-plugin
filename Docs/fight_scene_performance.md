# Matinee fight scene performance

|               | ACL Plugin v2.0.0 | ACL Plugin v1.0.0 | UE v4.25.0 |
| -------               | --------  | -------               | -------               |
| **Compressed size**   | 8.18 MB | 8.18 MB | 22.89 MB |
| **Compression ratio** | 7.62 : 1 | 7.63 : 1 | 2.73 : 1 |
| **Compression time**  | 4.80s | 6.82s | 4m 32.59s |
| **Compression speed** | 13295.33 KB/sec | 9362.83 KB/sec | 234.33 KB/sec |
| **Max ACL error**     | 0.0635 cm | 0.0634 cm | 0.0756 cm |
| **Max UE4 error**     | 0.0684 cm | 0.0684 cm | 0.0910 cm |
| **ACL Error 99<sup>th</sup> percentile** | 0.0201 cm | 0.0201 cm | 0.0162 cm |
| **Samples below ACL error threshold** | 97.83 % | 97.91 % | 94.13 % |

ACL was smaller for **1** clip (**20 %**)  
ACL was more accurate for **4** clips (**80 %**)  
ACL has faster compression for **5** clips (**100 %**)  
ACL was smaller, better, and faster for **1** clip (**20 %**)  

Would the *ACL Plugin* have been included in the *Automatic Compression* permutations tried, it would have won for **4** clips (**80 %**)

## Data and method used

To compile these statistics, the [Matinee fight scene](https://nfrechette.github.io/2017/10/05/acl_in_ue4/) is used.

*  Number of clips: **5**
*  Sample rate: **30 FPS**
*  Cinematic duration: **66 seconds**
*  *Troopers* 1-4 have **71** bones and the *Main Trooper* has **551** bones

To measure and extract the compression statistics, the provided [commandlet](../ACLPlugin/Source/ACLPluginEditor/Classes/ACLStatsDumpCommandlet.h) is used along with a [python script](../ACLPlugin/Extras/stat_parser.py) to parse the results.

The *ACL Plugin* uses the default settings with an error threshold of **0.01cm** while *UE4* uses the *Automatic Compression* with an error threshold (master tolerance) of **0.1 cm**. Both error thresholds used are suitable for production use. The **99th** percentile and the number of samples below the ACL error threshold are calculated by measuring the error with ACL on every bone at every sample.

*ACL* and *UE4* both use separate methods to measure the error and both values are shown for transparency.

