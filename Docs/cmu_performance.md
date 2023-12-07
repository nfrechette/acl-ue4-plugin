# Carnegie-Mellon University database performance

|                 | ACL Plugin v2.1.0 | ACL Plugin v2.0.0 | UE v5.2.0 |
| -------                | --------   | --------      | --------      |
| **Compressed size**    | 67.92 MB | 75.55 MB | 99.74 MB |
| **Compression ratio**  | 21.05 : 1 | 18.92 : 1 | 14.33 : 1 |
| **Compression time**   | 2m 48.78s | 2m 19.06s | 5m 54.28s |
| **Compression speed**  | 8671.99 KB/sec | 10525.35 KB/sec | 4131.39 KB/sec |
| **Max ACL error**      | 0.1299 cm | 0.0833 cm cm | 0.1520 cm |
| **Max UE error**      | 0.0662 cm | 0.0816 cm | 0.0995 cm |
| **ACL Error 99<sup>th</sup> percentile** | 0.0092 cm | 0.0088 cm | 0.0283 cm |
| **Samples below ACL error threshold** | 99.61 % | 99.93 % | 49.12 % |

ACL was smaller for **2533** clips (**99.96 %**)  
ACL was more accurate for **2500** clips (**98.66 %**)  
ACL has faster compression for **2518** clips (**99.37 %**)  
ACL was smaller, better, and faster for **2483** clips (**97.99 %**)  

Would the *ACL Plugin* have been included in the *Automatic Compression* permutations tried, it would have won for **2534** clips (**100.00 %**)

**Note: Numbers for ACL 2.0 were extracted with UE 4.25.**

## Data and method used

To compile the statistics, the [animation database from Carnegie-Mellon University](http://mocap.cs.cmu.edu/) is used.
The raw animation clips in FBX form can be found on the Unity asset store [here](https://www.assetstore.unity3d.com/en/#!/content/19991).
They were converted to the [ACL file format](the_acl_file_format.md) using the [fbx2acl](https://github.com/nfrechette/acl/tree/develop/tools/fbx2acl) script. Data available upon request, it is far too large for GitHub.

*  Number of clips: **2534**
*  Sample rate: **24 FPS**
*  Total duration: **9h 49m 37.58s**
*  Raw size: **1429.38 MB** (10x float32 * num bones * num samples)

To measure and extract the compression statistics, the provided [commandlet](../ACLPlugin/Source/ACLPluginEditor/Classes/ACLStatsDumpCommandlet.h) is used along with a [python script](../ACLPlugin/Extras/stat_parser.py) to parse the results.

The *ACL Plugin* uses the default settings with an error threshold of **0.01cm** while *UE* uses the *Automatic Compression* with an error threshold (master tolerance) of **0.1 cm**. Both error thresholds used are suitable for production use. The **99th** percentile and the number of samples below the ACL error threshold are calculated by measuring the error with ACL on every bone at every sample.

*ACL* and *UE* both use separate methods to measure the error and both values are shown for transparency.
