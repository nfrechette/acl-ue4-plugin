# Carnegie-Mellon University database performance

|                 | ACL Plugin v0.1.0 | UE 4.19.2     |
| -------                | --------   | --------      |
| **Compressed size**    | 70.60 MB   | 99.94 MB      |
| **Compression ratio**  | 20.25 : 1  | 14.30 : 1     |
| **Max ACL error**      | 0.0703 cm  | 0.1520 cm     |
| **Max UE4 error**      | 0.0722 cm  | 0.0996 cm     |
| **Compression time**   | 34m 30.51s | 1h 27m 40.15s |

ACL was smaller for **2532** clips (**99.92 %**)  
ACL was more accurate for **2486** clips (**98.11 %**)  
ACL has faster compression for **2534** clips (**100.00 %**)  
ACL was smaller, better, and faster for **2484** clips (**98.03 %**)  

Would the *ACL Plugin* have been included in the *Automatic Compression* permutations tried, it would have won for **2534** clips (**100.00 %**)

## Data and method used

To compile the statistics, the [animation database from Carnegie-Mellon University](http://mocap.cs.cmu.edu/) is used.
The raw animation clips in FBX form can be found on the Unity asset store [here](https://www.assetstore.unity3d.com/en/#!/content/19991).
They were converted to the [ACL file format](the_acl_file_format.md) using the [fbx2acl](https://github.com/nfrechette/acl/tree/develop/tools/fbx2acl) script. Data available upon request, it is far too large for GitHub.

*  Number of clips: **2534**
*  Sample rate: **24 FPS**
*  Total duration: **9h 49m 37.58s**
*  Raw size: **1429.38 MB** (10x float32 * num bones * num samples)

To measure and extract the compression statistics, the provided [commandlet](../ACLPlugin/Source/ACLPlugin/Classes/ACLStatsDumpCommandlet.h) is used.

The *ACL Plugin* uses the default settings while *UE4* uses the *Automatic Compression* with an error threshold (master tolerance) of **0.1 cm**.

*ACL* and *UE4* both use separate methods to measure the error and both values are shown for transparency.