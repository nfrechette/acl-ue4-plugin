# Matinee fight scene performance

|               | ACL Plugin v0.5.0 | ACL Plugin v0.4.0 | UE v4.23.1 |
| -------               | --------  | -------               | -------               |
| **Compressed size**   | 8.18 MB | 8.77 MB | 23.67 MB   |
| **Compression ratio** | 7.63 : 1 | 7.11 : 1 | 2.63 : 1   |
| **Compression time**  | 7.16s | 16.85s | 6m 18.56s |
| **Compression speed** | 8918.59 KB/sec | 3790.85 KB/sec | 168.73 KB/sec |
| **Max ACL error**     | 0.0634 cm | 0.0588 cm | 0.0426 cm |
| **Max UE4 error**     | 0.0684 cm | 0.0617 cm | 0.0672 cm  |
| **ACL Error 99<sup>th</sup> percentile** | 0.0201 cm | 0.0382 cm | 0.0161 cm |
| **Samples below ACL error threshold** | 97.91 % | 94.61 % | 94.20 % |

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

To measure and extract the compression statistics, the provided [commandlet](../ACLPlugin/Source/ACLPlugin/Classes/ACLStatsDumpCommandlet.h) is used along with a [python script](../Tools/stat_parser.py) to parse the results.

The *ACL Plugin* uses the default settings with an error threshold of **0.01cm** while *UE4* uses the *Automatic Compression* with an error threshold (master tolerance) of **0.1 cm**. Both error thresholds used are suitable for production use. The **99th** percentile and the number of samples below the ACL error threshold are calculated by measuring the error with ACL on every bone at every sample.

*ACL* and *UE4* both use separate methods to measure the error and both values are shown for transparency.

