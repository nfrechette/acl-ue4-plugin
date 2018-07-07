# Matinee fight scene performance

To compile these statistics, the [Matinee fight scene](http://nfrechette.github.io/2017/10/05/acl_in_ue4/) is used.

*  Number of clips: **5**
*  Sample rate: **30 FPS**
*  Cinematic duration: **66 seconds**
*  *Troopers* 1-4 have **71** bones and the *Main Trooper* has **551** bones

To measure and extract the compression statistics, the provided [commandlet](../ACLPlugin/Source/ACLPlugin/Classes/ACLStatsDumpCommandlet.h) is used.

The *ACL Plugin* uses the default settings while *UE4* uses the *Automatic Compression* with an error threshold (master tolerance) of **0.1 cm**.

|         | ACL Plugin v0.1.0 | UE 4.19.2 |
| ------- | -------- | -------- |
| **Compressed size**      | 0 MB | 0 MB |
| **Compression ratio**    | 0 : 1 | 0 : 1 |
| **Max error**            | 0.0 cm | 0.0 cm |
| **Compression time**     | 0s | 0s |

ACL was smaller for **0** clips (**0 %**)  
ACL was more accurate for **0** clips (**0 %**)  
ACL has faster compression for **0** clips (**0 %**)  
ACL was smaller, better, and faster for **0** clips (**0 %**)  

Would the *ACL Plugin* have been included in the *Automatic Compression* permutations tried, it would have won for **0** clips (**0 %**)

**Results from release [v0.1.0](https://github.com/nfrechette/acl-ue4-plugin/releases/tag/v0.1.0)**
