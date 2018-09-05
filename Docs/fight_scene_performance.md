# Matinee fight scene performance

|               | ACL Plugin v0.2.0 | ACL Plugin v0.1.0 | UE v4.19.2 |
| -------               | --------  | --------   | -------               |
| **Compressed size**   | 8.67 MB | 8.67 MB   | 23.67 MB   |
| **Compression ratio** | 7.20 : 1 | 7.20 : 1  | 2.63 : 1   |
| **Compression time**  | 48.42s | 52.44s    | 54m 03.18s |
| **Compression speed** | 1319.21 KB/sec | 1217.99 KB/sec | 19.70 KB/sec |
| **Max ACL error**     | 0.0620 cm | 0.0620 cm | 0.0671 cm  |
| **Max UE4 error**     | 0.0678 cm | 0.0674 cm | 0.0672 cm  |
| **ACL Error 99<sup>th</sup> percentile** | 0.0255 cm | 0.0255 cm | 0.0161 cm |
| **Samples below ACL error threshold** | 95.06 % | 95.05 % | 94.22 % |

ACL was smaller for **1** clip (**20 %**)  
ACL was more accurate for **2** clips (**40 %**)  
ACL has faster compression for **5** clips (**100 %**)  
ACL was smaller, better, and faster for **0** clip (**0 %**)  

Would the *ACL Plugin* have been included in the *Automatic Compression* permutations tried, it would have won for **3** clips (**60 %**)

## Data and method used

To compile these statistics, the [Matinee fight scene](http://nfrechette.github.io/2017/10/05/acl_in_ue4/) is used.

*  Number of clips: **5**
*  Sample rate: **30 FPS**
*  Cinematic duration: **66 seconds**
*  *Troopers* 1-4 have **71** bones and the *Main Trooper* has **551** bones

To measure and extract the compression statistics, the provided [commandlet](../ACLPlugin/Source/ACLPlugin/Classes/ACLStatsDumpCommandlet.h) is used along with a [python script](../Tools/stat_parser.py) to parse the results.

The *ACL Plugin* uses the default settings with an error threshold of **0.01cm** while *UE4* uses the *Automatic Compression* with an error threshold (master tolerance) of **0.1 cm**. Both error thresholds used are suitable for production use. The **99th** percentile and the number of samples below the ACL error threshold are calculated by measuring the error with ACL on every bone at every sample.

*ACL* and *UE4* both use separate methods to measure the error and both values are shown for transparency.

