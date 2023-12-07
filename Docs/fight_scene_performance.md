# Matinee fight scene performance

|               | ACL Plugin v2.1.0 | ACL Plugin v2.0.0 | UE v5.2.0 |
| -------               | --------  | -------               | -------               |
| **Compressed size**   | 8.06 MB | 8.18 MB | 23.68 MB |
| **Compression ratio** | 7.74 :1 | 7.62 : 1 | 2.63 : 1 |
| **Compression time**  | 9.08s | 4.80s | 1m 35.19s |
| **Compression speed** | 7036.24 KB/sec | 13295.33 KB/sec | 671.02 KB/sec |
| **Max ACL error**     | 0.1215 cm | 0.0635 cm | 0.1186 cm |
| **Max UE error**     | 0.0480 cm | 0.0684 cm | 0.0562 cm |
| **ACL Error 99<sup>th</sup> percentile** | 0.0245 cm | 0.0201 cm | 0.0231 cm |
| **Samples below ACL error threshold** | 94.99 % | 97.83 % | 90.49 % |

ACL was smaller for **4** clip (**80 %**)  
ACL was more accurate for **4** clips (**80 %**)  
ACL has faster compression for **5** clips (**100 %**)  
ACL was smaller, better, and faster for **3** clip (**60 %**)  

Would the *ACL Plugin* have been included in the *Automatic Compression* permutations tried, it would have won for **5** clips (**100 %**)

**Note: Numbers for ACL 2.0 were extracted with UE 4.25.**

## Data and method used

To compile these statistics, the [Matinee fight scene](https://nfrechette.github.io/2017/10/05/acl_in_ue4/) is used.

*  Number of clips: **5**
*  Sample rate: **30 FPS**
*  Cinematic duration: **66 seconds**
*  *Troopers* 1-4 have **71** bones and the *Main Trooper* has **551** bones

To measure and extract the compression statistics, the provided [commandlet](../ACLPlugin/Source/ACLPluginEditor/Classes/ACLStatsDumpCommandlet.h) is used along with a [python script](../ACLPlugin/Extras/stat_parser.py) to parse the results.

The *ACL Plugin* uses the default settings with an error threshold of **0.01cm** while *UE* uses the *Automatic Compression* with an error threshold (master tolerance) of **0.1 cm**. Both error thresholds used are suitable for production use. The **99th** percentile and the number of samples below the ACL error threshold are calculated by measuring the error with ACL on every bone at every sample.

*ACL* and *UE* both use separate methods to measure the error and both values are shown for transparency.

