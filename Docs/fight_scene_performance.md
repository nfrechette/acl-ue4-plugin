# Matinee fight scene performance

|         | ACL Plugin v0.1.0 | UE 4.19.2 |
| ------- | -------- | -------- |
| **Compressed size**      | 8.67 MB | 19.04 MB |
| **Compression ratio**    | 7.20 : 1 | 3.28 : 1 |
| **Max error**            | 0.067 cm | 0.161 cm |
| **Compression time**     | 1m 38.5s | 1h 29m 54.6s |

ACL was smaller for **1** clip (**20 %**)  
ACL was more accurate for **4** clips (**80 %**)  
ACL has faster compression for **5** clips (**100 %**)  
ACL was smaller, better, and faster for **1** clip (**20 %**)  

Would the *ACL Plugin* have been included in the *Automatic Compression* permutations tried, it would have won for **4** clips (**80 %**)

**Results from release [v0.1.0](https://github.com/nfrechette/acl-ue4-plugin/releases/tag/v0.1.0)**

## Data and method used

To compile these statistics, the [Matinee fight scene](http://nfrechette.github.io/2017/10/05/acl_in_ue4/) is used.

*  Number of clips: **5**
*  Sample rate: **30 FPS**
*  Cinematic duration: **66 seconds**
*  *Troopers* 1-4 have **71** bones and the *Main Trooper* has **551** bones

To measure and extract the compression statistics, the provided [commandlet](../ACLPlugin/Source/ACLPlugin/Classes/ACLStatsDumpCommandlet.h) is used.

The *ACL Plugin* uses the default settings while *UE4* uses the *Automatic Compression* with an error threshold (master tolerance) of **0.1 cm**.
