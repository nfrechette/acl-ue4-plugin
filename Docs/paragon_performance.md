# Paragon database performance

|         | ACL Plugin v0.1.0 | UE 4.19.2 |
| ------- | -------- | -------- |
| **Compressed size**      | 220.75 MB | 392.97 MB |
| **Compression ratio**    | 19.37 : 1 | 10.88 : 1 |
| **Max error**            | 0.9457 cm | 0.8619 cm |
| **Compression time**     | 5h 51m 32.68s | 13h 43m 57.80s |

ACL was smaller for **6415** clips (**97.82 %**)  
ACL was more accurate for **5107** clips (**77.87 %**)  
ACL has faster compression for **5972** clips (**91.06 %**)  
ACL was smaller, better, and faster for **4645** clips (**70.83 %**)  

Would the *ACL Plugin* have been included in the *Automatic Compression* permutations tried, it would have won for **5766** clips (**87.92 %**)

**Results from release [v0.1.0](https://github.com/nfrechette/acl-ue4-plugin/releases/tag/v0.1.0)**

## Data and method used

To compile these statistics, a large number of animations from [Paragon](https://www.epicgames.com/paragon) are used.
In October 2017 the animations were manually extracted and converted to the [ACL file format](https://github.com/nfrechette/acl/blob/develop/docs/the_acl_file_format.md) losslessly. The data is sadly **NOT** available upon request.
Epic has permitted [Nicholas Frechette](https://github.com/nfrechette) to use them for research purposes only under a non-disclosure agreement.

**Note: Epic has since released Paragon assets publicly in early 2018, once I get around to it, it will be extracted and made available along with updated stats.**

*  Number of clips: **6558**
*  Total duration: **7h 00m 45.27s**
*  Raw size: **4276.11 MB** (10x float32 * num bones * num samples)

The data set contains among other things:

*  Lots of characters with varying number of bones
*  Animated objects of various shape and form
*  Very short and very long clips
*  Clips with unusual sample rate (as low as **2** FPS!)
*  World space clips
*  Lots of 3D scale
*  Lots of other exotic clips

To measure and extract the compression statistics, the provided [commandlet](../ACLPlugin/Source/ACLPlugin/Classes/ACLStatsDumpCommandlet.h) is used.

The *ACL Plugin* uses the default settings while *UE4* uses the *Automatic Compression* with an error threshold (master tolerance) of **0.1 cm**.
