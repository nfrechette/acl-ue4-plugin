# A Boy and His Kite performance

|                                      | Compressed Size | Compression Rate |
| ------------------------------------ | --------------- | ---------------- |
| **UE Compressed Rich Curves 0.0**   | 3620.66 KB      | 1.0x (baseline)  |
| **UE Compressed Rich Curves 0.001** | 1458.77 KB      | 2.5x smaller     |
| **UE Uniform Sampling**             | 2052.82 KB      | 1.8x smaller     |
| **ACL 0.001**                        | 540.24 KB       | 6.7x smaller     |
| **ACL with morph 0.01 cm**           | 381.10 KB       | 9.5x smaller     |

*ACL Plugin v1.0* and *UE 4.25* were used to gather these statistics.

## Data and method used

To compile these statistics, the GDC 2015 demo from *Epic* [A Boy and His Kite](https://www.youtube.com/watch?v=JNgsbNvkNjE) is used.

*  Number of clips: **31**
*  Number of animated curves: **811**
*  Number of morph target curves: **692**

To measure and extract the compression statistics, UE was manually instrumented to print out the compressed size of the curve data.

The *ACL Plugin* uses the default settings with a morph target precision of **0.01cm** and a curve precision of **0.001**. Both values are suitable for production use. Numbers above are shown with and without the morph target precision enabled.

The *UE Compressed Rich Curve* uses an error threshold of **0.0** or **0.001**.

The *UE Uniform Sampling* uses default values.
