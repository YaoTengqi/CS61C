#include "matrix.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <omp.h>

// Include SSE intrinsics
#if defined(_MSC_VER)
#include <intrin.h>
#elif defined(__GNUC__) && (defined(__x86_64__) || defined(__i386__))
#include <immintrin.h>
#include <x86intrin.h>
#endif

/* Below are some intel intrinsics that might be useful
 * void _mm256_storeu_pd (double * mem_addr, __m256d a)
 * __m256d _mm256_set1_pd (double a)
 * __m256d _mm256_set_pd (double e3, double e2, double e1, double e0)
 * __m256d _mm256_loadu_pd (double const * mem_addr)
 * __m256d _mm256_add_pd (__m256d a, __m256d b)
 * __m256d _mm256_sub_pd (__m256d a, __m256d b)
 * __m256d _mm256_fmadd_pd (__m256d a, __m256d b, __m256d c)
 * __m256d _mm256_mul_pd (__m256d a, __m256d b)
 * __m256d _mm256_cmp_pd (__m256d a, __m256d b, const int imm8)
 * __m256d _mm256_and_pd (__m256d a, __m256d b)
 * __m256d _mm256_max_pd (__m256d a, __m256d b)
 */

/*
 * Generates a random double between `low` and `high`.
 */
double rand_double(double low, double high)
{
    double range = (high - low);
    double div = RAND_MAX / range;
    return low + (rand() / div);
}

/*
 * Generates a random matrix with `seed`.
 */
void rand_matrix(matrix *result, unsigned int seed, double low, double high)
{
    srand(seed);
    for (int i = 0; i < result->rows; i++)
    {
        for (int j = 0; j < result->cols; j++)
        {
            set(result, i, j, rand_double(low, high));
        }
    }
}

/*
 * Allocate space for a matrix struct pointed to by the double pointer mat with
 * `rows` rows and `cols` columns. You should also allocate memory for the data array
 * and initialize all entries to be zeros. Remember to set all fieds of the matrix struct.
 * `parent` should be set to NULL to indicate that this matrix is not a slice.
 * You should return -1 if either `rows` or `cols` or both have invalid values, or if any
 * call to allocate memory in this function fails. If you don't set python error messages here upon
 * failure, then remember to set it in numc.c.
 * Return 0 upon success and non-zero upon failure.
 */
int allocate_matrix(matrix **mat, int rows, int cols)
{
    if (rows <= 0 || cols <= 0)
    {
        printf("\nRows or/and cols are invalid");
        return -1; // 出现了错误矩阵情况
    }

    // 给数据结构matrix分配内存
    *mat = (matrix *)malloc(sizeof(matrix));

    if (*mat == NULL)
    {
        printf("\nError when alloc memory for *mat");
        return -1; // 分配错误时 throw a runtime error
    }

    // 初始化矩阵
    (*mat)->cols = cols;
    (*mat)->rows = rows;
    (*mat)->parent = NULL;
    (*mat)->ref_cnt = 1;
    if (cols == 1 || rows == 1)
    {
        (*mat)->is_1d = 1;
    }
    else
    {
        (*mat)->is_1d = 0;
    }

    (*mat)->data = (double **)malloc(rows * sizeof(double *));
    if ((*mat)->data == NULL)
    {
        printf("\nError when alloc memory for (*mat)->data");
        free((*mat)); // 先释放掉原来分配的内存空间
        return -1;
    }
    for (int i = 0; i < rows; i++)
    {
        (*mat)->data[i] = (double *)malloc(cols * sizeof(double));
        if ((*mat)->data[i] == NULL)
        {
            printf("\nError when alloc memory for (*mat)->data[i]");
            free((*mat)->data);
            free((*mat)); // 释放掉原来分配的内存空间
            return -1;
        }
        for (int j = 0; j < cols; j++)
        {
            (*mat)->data[i][j] = 0;
        }
    }

    return 0;
}

/*
 * Allocate space for a matrix struct pointed to by `mat` with `rows` rows and `cols` columns.
 * This is equivalent to setting the new matrix to be
 * from[row_offset:row_offset + rows, col_offset:col_offset + cols]
 * If you don't set python error messages here upon failure, then remember to set it in numc.c.
 * Return 0 upon success and non-zero upon failure.
 */
int allocate_matrix_ref(matrix **mat, matrix *from, int row_offset, int col_offset, int rows, int cols)
{
    if (rows <= 0 || cols <= 0)
    {
        printf("\nRows or/and cols are invalid");
        return 1; // 出现了错误矩阵情况
    }

    // 给数据结构matrix分配内存
    *mat = (matrix *)malloc(sizeof(matrix));
    if (mat == NULL)
    {
        printf("\nError when alloc memory for *mat");
        return 1;
    }

    (*mat)->cols = cols;
    (*mat)->rows = rows;
    (*mat)->ref_cnt = 1;
    (*mat)->parent = from;
    (*mat)->parent->ref_cnt = (*mat)->parent->ref_cnt + (*mat)->ref_cnt;

    if (rows == 1 || cols == 1)
    {
        (*mat)->is_1d = 1;
    }
    else
    {
        (*mat)->is_1d = 0;
    }

    (*mat)->data = (double **)malloc(rows * sizeof(double *));
    if ((*mat)->data == NULL)
    {
        printf("\nError when alloc memory for (*mat)->data");
        free(*mat);
        return -1;
    }

    for (int i = 0; i < rows; i++)
    {
        (*mat)->data[i] = (double *)malloc(cols * sizeof(double));
        if ((*mat)->data[i] == NULL)
        {
            free((*mat)->data);
            free(*mat);
            printf("\nError when alloc memory for (*mat)->data[i]");
            return -1;
        }

        for (int j = 0; j < cols; j++)
        {
            (*mat)->data[i][j] = from->data[i + row_offset][j + col_offset];
        }
    }

    return 0;
}

/*
 * This function will be called automatically by Python when a numc matrix loses all of its
 * reference pointers.
 * You need to make sure that you only free `mat->data` if no other existing matrices are also
 * referring this data array.
 * See the spec for more information.
 */
void deallocate_matrix(matrix *mat)
{
    if (mat == NULL)
    {
        printf("\n*mat is NULL");
    }
    else if (mat->ref_cnt == 1 && mat->parent == NULL)
    {
        for (int i = 0; i < mat->rows; i++)
        {
            free(mat->data[i]);
        }
        free(mat->data);
    }
    else
    {
        printf("\nThere has at least 1 matrice is referring this data array");
    }
}

/*
 * Return the double value of the matrix at the given row and column.
 * You may assume `row` and `col` are valid.
 */
double get(matrix *mat, int row, int col)
{
    if (mat == NULL)
    {
        printf("\n*mat is NULL");
        {
            return -1;
        }
    }
    return mat->data[row][col];
}

/*
 * Set the value at the given row and column to val. You may assume `row` and
 * `col` are valid
 */
void set(matrix *mat, int row, int col, double val)
{
    mat->data[row][col] = val;
}

/*
 * Set all entries in mat to val
 */
void fill_matrix(matrix *mat, double val)
{
    for (int i = 0; i < mat->rows; i++)
    {
        for (int j = 0; j < mat->cols; j++)
        {
            mat->data[i][j] = val;
        }
    }
}

/*
 * Store the result of adding mat1 and mat2 to `result`.
 * Return 0 upon success and a nonzero value upon failure.
 */
int add_matrix(matrix *result, matrix *mat1, matrix *mat2)
{
    if (mat1 == NULL || mat2 == NULL || result == NULL)
    {
        printf("\n*mat is NULL");
        {
            return -1;
        }
    }
    int rows1, rows2, cols1, cols2;
    rows1 = mat1->rows;
    cols1 = mat1->cols;
    rows2 = mat2->rows;
    cols2 = mat2->cols;
    if (rows1 != rows2 || cols1 != cols2)
    {
        printf("\nRows or/and cols are not equal");
        return -1;
    }
    for (int i = 0; i < rows1; i++)
    {
        for (int j = 0; j < cols1; j++)
        {
            result->data[i][j] = mat1->data[i][j] + mat2->data[i][j];
        }
    }
    return 0;
}

/*
 * Store the result of subtracting mat2 from mat1 to `result`.
 * Return 0 upon success and a nonzero value upon failure.
 */
int sub_matrix(matrix *result, matrix *mat1, matrix *mat2)
{
    if (mat1 == NULL || mat2 == NULL || result == NULL)
    {
        printf("\n*mat is NULL");
        {
            return -1;
        }
    }
    int rows1, rows2, cols1, cols2;
    rows1 = mat1->rows;
    cols1 = mat1->cols;
    rows2 = mat2->rows;
    cols2 = mat2->cols;
    if (rows1 != rows2 || cols1 != cols2)
    {
        printf("\nRows or/and cols are not equal");
        return -1;
    }
    for (int i = 0; i < rows1; i++)
    {
        for (int j = 0; j < cols1; j++)
        {
            result->data[i][j] = mat1->data[i][j] - mat2->data[i][j];
        }
    }
    return 0;
}

/*
 * Store the result of multiplying mat1 and mat2 to `result`.
 * Return 0 upon success and a nonzero value upon failure.
 * Remember that matrix multiplication is not the same as multiplying individual elements.
 */
int mul_matrix(matrix *result, matrix *mat1, matrix *mat2)
{
    if (mat1 == NULL || mat2 == NULL || result == NULL)
    {
        printf("\n*mat is NULL");
        {
            return -1;
        }
    }
    int rows1, rows2, rows_result, cols1, cols2, cols_result;
    rows1 = mat1->rows;
    cols1 = mat1->cols;
    rows2 = mat2->rows;
    cols2 = mat2->cols;
    rows_result = result->rows;
    cols_result = result->cols;
    if (rows1 != rows_result || cols1 != rows2 || cols2 != cols_result)
    {
        printf("\nmul_matrix:rows or/and cols are not equal");
        return -1;
    }
    for (int k = 0; k < cols2; k++)
    {
        for (int i = 0; i < rows1; i++)
        {
            for (int j = 0; j < cols1; j++)
            {
                result->data[i][k] += mat1->data[i][j] * mat2->data[j][k];
            }
        }
    }
    return 0;
}

/*
 * Store the result of raising mat to the (pow)th power to `result`.
 * Return 0 upon success and a nonzero value upon failure.
 * Remember that pow is defined with matrix multiplication, not element-wise multiplication.
 */
int pow_matrix(matrix *result, matrix *mat, int pow)
{
    if (mat->cols != mat->rows)
    {
        printf("\npow_matrix:rows is not equal cols");
        return -1;
    }
    matrix *temp;
    allocate_matrix(&temp, mat->rows, mat->cols);
    fill_matrix(temp, 0);
    // 将矩阵初始化为单位矩阵
    for (int i = 0; i < mat->rows; i++)
    {
        for (int j = 0; j < mat->cols; j++)
        {
            if (i == j)
            {
                result->data[i][j] = 1.0;
            }
            else
            {
                result->data[i][j] = 0.0;
            }
        }
    }

    for (int i = 0; i < pow; i++)
    {
        mul_matrix(temp, result, mat);
        for (int i = 0; i < mat->rows; i++)
        {
            for (int j = 0; j < mat->cols; j++)
            {
                result->data[i][j] = temp->data[i][j];
            }
        }
        fill_matrix(temp, 0); // 再次初始化temp矩阵
    }

    deallocate_matrix(temp);
    return 0;
}

/*
 * Store the result of element-wise negating mat's entries to `result`.
 * Return 0 upon success and a nonzero value upon failure.
 */
int neg_matrix(matrix *result, matrix *mat)
{
    if (mat == NULL)
    {
        printf("\n*mat is NULL");
        {
            return -1;
        }
    }
    for (int i = 0; i < mat->rows; i++)
    {
        for (int j = 0; j < mat->cols; j++)
        {
            result->data[i][j] = -mat->data[i][j];
        }
    }
    return 0;
}

/*
 * Store the result of taking the absolute value element-wise to `result`.
 * Return 0 upon success and a nonzero value upon failure.
 */
int abs_matrix(matrix *result, matrix *mat)
{
    if (mat == NULL)
    {
        printf("\n*mat is NULL");
        {
            return -1;
        }
    }
    for (int i = 0; i < mat->rows; i++)
    {
        for (int j = 0; j < mat->cols; j++)
        {
            if (mat->data[i][j] < 0)
            {
                result->data[i][j] = -mat->data[i][j];
            }
            else
            {
                result->data[i][j] = mat->data[i][j];
            }
        }
    }
    return 0;
}
