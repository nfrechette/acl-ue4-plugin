# Carnegie-Mellon University database performance

|                 | ACL Plugin v2.0.0 | ACL Plugin v1.0.0 | UE v4.25.0 |
| -------                | --------   | --------      | --------      |
| **Compressed size**    | 75.55 MB | 74.40 MB | 100.70 MB |
| **Compression ratio**  | 18.92 : 1 | 19.21 : 1 | 14.19 : 1 |
| **Compression time**   | 2m 19.06s | 2m 42.97s | 7m 10.29s |
| **Compression speed**  | 10525.35 KB/sec | 8981.37 KB/sec | 3401.59 KB/sec |
| **Max ACL error**      | 0.0833 cm | 0.0969 cm | 0.1675 cm |
| **Max UE4 error**      | 0.0816 cm | 0.0816 cm | 0.0995 cm |
| **ACL Error 99<sup>th</sup> percentile** | 0.0088 cm | 0.0089 cm | 0.0304 cm |
| **Samples below ACL error threshold** | 99.93 % | 99.90 % | 47.89 % |

ACL was smaller for **2521** clips (**99.49 %**)  
ACL was more accurate for **2504** clips (**98.82 %**)  
ACL has faster compression for **2494** clips (**98.42 %**)  
ACL was smaller, better, and faster for **2453** clips (**96.80 %**)  

Would the *ACL Plugin* have been included in the *Automatic Compression* permutations tried, it would have won for **2534** clips (**100.00 %**)

## Data and method used

To compile the statistics, the [animation database from Carnegie-Mellon University](http://mocap.cs.cmu.edu/) is used.
The raw animation clips in FBX form can be found on the Unity asset store [here](https://www.assetstore.unity3d.com/en/#!/content/19991).
They were converted to the [ACL file format](the_acl_file_format.md) using the [fbx2acl](https://github.com/nfrechette/acl/tree/develop/tools/fbx2acl) script. Data available upon request, it is far too large for GitHub.

*  Number of clips: **2534**
*  Sample rate: **24 FPS**
*  Total duration: **9h 49m 37.58s**
*  Raw size: **1429.38 MB** (10x float32 * num bones * num samples)

To measure and extract the compression statistics, the provided [commandlet](../ACLPlugin/Source/ACLPlugin/Classes/ACLStatsDumpCommandlet.h) is used along with a [python script](../Tools/stat_parser.py) to parse the results.

The *ACL Plugin* uses the default settings with an error threshold of **0.01cm** while *UE4* uses the *Automatic Compression* with an error threshold (master tolerance) of **0.1 cm**. Both error thresholds used are suitable for production use. The **99th** percentile and the number of samples below the ACL error threshold are calculated by measuring the error with ACL on every bone at every sample.

*ACL* and *UE4* both use separate methods to measure the error and both values are shown for transparency.