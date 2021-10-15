/* 
 * trans.c - Matrix transpose B = A^T
 *
 * Each transpose function must have a prototype of the form:
 * void trans(int M, int N, int A[N][M], int B[M][N]);
 *
 * A transpose function is evaluated by counting the number of misses
 * on a 1KB direct mapped cache with a block size of 32 bytes.
 */ 
#include <stdio.h>
#include "cachelab.h"

int is_transpose(int M, int N, int A[N][M], int B[M][N]);

/* 
 * transpose_submit - This is the solution transpose function that you
 *     will be graded on for Part B of the assignment. Do not change
 *     the description string "Transpose submission", as the driver
 *     searches for that string to identify the transpose function to
 *     be graded. 
 */
char transpose_submit_desc[] = "Transpose submission";
void transpose_submit(int M, int N, int A[N][M], int B[M][N])
{
	int nn, mm, n, m;
	int t0, t1, t2, t3, t4, t5, t6, t7;

	// divide arrays into 8*8 blocks
	if (N == 32) {
		for (nn = 0; nn < N; nn += 8) {
			for (mm = 0; mm < M; mm += 8) {
				for (n = 0; n < 8; n++) {
					for (m = 0; m < 8; m++) {
						// directly copy non-diagonal elements
						if (nn + n != mm + m) {
							B[mm + m][nn + n] = A[nn + n][mm + m];
						}
						// copy diagonal elements via stack
						else {
							t0 = nn + n;
							t1 = A[t0][t0];
						}
					}
					if (nn == mm) B[t0][t0] = t1;
				}
			}
		}
	}
	else if (N == 64) {
		for (nn = 0; nn < N; nn += 8) {
			for (mm = 0; mm < M; mm += 8) {
				// non-diagonal blocks
				if (nn != mm) {
					// divide the block again into four 4*4 quadrants
					// *----*----*			*----*----*
					// | a1 | a2 |			| b1 | b2 |
					// *----*----*			*----*----*
					// | a3 | a4 |			| b3 | b4 |
					// *----*----*			*----*----*
					// copy a1 to b1, a2 to b2
					for (n = 0; n < 4; n++)
						for (m = 0; m < 8; m++)
							B[mm + n][nn + m] = A[nn + n][mm + m];
					// transpose b1, b2 in-place
					for (n = 0; n < 4; n++) {
						for (m = n + 1; m < 4; m++) {
							t0 = B[mm + n][nn + m];
							B[mm + n][nn + m] = B[mm + m][nn + n];
							B[mm + m][nn + n] = t0;

							t0 = B[mm + n][nn + 4 + m];
							B[mm + n][nn + 4 + m] = B[mm + m][nn + 4 + n];
							B[mm + m][nn + 4 + n] = t0;
						}
					}
					// transpose-copy a3 to b2 and simultaneously move b2 to b3 via stack
					for (m = 0; m < 4; m++) {
						t0 = A[nn + 4][mm + m];
						t1 = A[nn + 5][mm + m];
						t2 = A[nn + 6][mm + m];
						t3 = A[nn + 7][mm + m];

						t4 = B[mm + m][nn + 4];
						t5 = B[mm + m][nn + 5];
						t6 = B[mm + m][nn + 6];
						t7 = B[mm + m][nn + 7];

						B[mm + m][nn + 4] = t0;
						B[mm + m][nn + 5] = t1;
						B[mm + m][nn + 6] = t2;
						B[mm + m][nn + 7] = t3;

						B[mm + 4 + m][nn + 0] = t4;
						B[mm + 4 + m][nn + 1] = t5;
						B[mm + 4 + m][nn + 2] = t6;
						B[mm + 4 + m][nn + 3] = t7;
					}
					// copy a4 to b4
					for (n = 4; n < 8; n++)
						for (m = 4; m < 8; m++)
							B[mm + m][nn + n] = A[nn + n][mm + m];
				}
				// diagonal blocks except the last one
				else if (nn + 8 < N) {
					// copy to next block b'
					for (n = 0; n < 8; n++)
						for (m = 0; m < 8; m++)
							B[nn + n][mm + m + 8] = A[nn + n][mm + m];
					// transpose-copy b'4 to b4, b'3 to b2
					for (m = 7; m >= 0; m--)
						for (n = 4; n < 8; n++)
							B[mm + m][nn + n] = B[nn + n][mm + m + 8];
					// transpose-copy b'1 to b1, b'2 to b3
					for (m = 0; m < 8; m++)
						for (n = 0; n < 4; n++)
							B[mm + m][nn + n] = B[nn + n][mm + m + 8];
				}
				// last diagonal block
				else {
					// transpose-copy via stack
					for (n = 0; n < 8; n++) {
						t0 = A[nn + n][mm + 0];
						t1 = A[nn + n][mm + 1];
						t2 = A[nn + n][mm + 2];
						t3 = A[nn + n][mm + 3];
						t4 = A[nn + n][mm + 4];
						t5 = A[nn + n][mm + 5];
						t6 = A[nn + n][mm + 6];
						t7 = A[nn + n][mm + 7];
						B[mm + 0][nn + n] = t0;
						B[mm + 1][nn + n] = t1;
						B[mm + 2][nn + n] = t2;
						B[mm + 3][nn + n] = t3;
						B[mm + 4][nn + n] = t4;
						B[mm + 5][nn + n] = t5;
						B[mm + 6][nn + n] = t6;
						B[mm + 7][nn + n] = t7;
					}
				}
			}
		}
	}
	else {
		// iterate column-wise over blocks
		for (mm = 0; mm < M; mm += 16) {
			for (nn = 0; nn < N; nn += 16) {
				for (n = 0; n < 16 && nn + n < N; n++) {
					for (m = 0; m < 16 && mm + m < M; m++) {
						B[mm + m][nn + n] = A[nn + n][mm + m];
					}
				}
			}
		}
	}
}

void registerFunctions()
{
    /* Register your solution function */
    registerTransFunction(transpose_submit, transpose_submit_desc); 
}

int is_transpose(int M, int N, int A[N][M], int B[M][N])
{
    int i, j;

    for (i = 0; i < N; i++) {
        for (j = 0; j < M; ++j) {
            if (A[i][j] != B[j][i]) {
                return 0;
            }
        }
    }
    return 1;
}

