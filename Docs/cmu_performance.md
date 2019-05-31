# Carnegie-Mellon University database performance

|                 | ACL Plugin v0.4.0 | ACL Plugin v0.3.0 | UE v4.22.2  |
| -------                | --------   | --------      | --------      |
| **Compressed size**    | 74.42 MB | 74.42 MB | 100.15 MB |
| **Compression ratio**  | 19.21 : 1 | 19.21 : 1 | 14.27 : 1   |
| **Compression time**   | 5m 10.57s | 6m 24.59s | 11m 11.17s |
| **Compression speed**  | 4712.83 KB/sec | 3805.81 KB/sec | 2180.80 KB/sec |
| **Max ACL error**      | 0.0968 cm | 0.0702 cm | 0.1675 cm  |
| **Max UE4 error**      | 0.0816 cm | 0.0816 cm | 0.0995 cm    |
| **ACL Error 99<sup>th</sup> percentile** | 0.0089 cm | 0.0088 cm | 0.0304 cm |
| **Samples below ACL error threshold** | 99.90 % | 99.93 % | 47.81 % |

ACL was smaller for **2532** clips (**99.92 %**)  
ACL was more accurate for **2501** clips (**98.70 %**)  
ACL has faster compression for **2534** clips (**100.00 %**)  
ACL was smaller, better, and faster for **2499** clips (**98.62 %**)  

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