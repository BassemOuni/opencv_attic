/*M///////////////////////////////////////////////////////////////////////////////////////
//
//  IMPORTANT: READ BEFORE DOWNLOADING, COPYING, INSTALLING OR USING.
//
//  By downloading, copying, installing or using the software you agree to this license.
//  If you do not agree to this license, do not download, install,
//  copy or use the software.
//
//
//                        Intel License Agreement
//                For Open Source Computer Vision Library
//
// Copyright (C) 2000, Intel Corporation, all rights reserved.
// Third party copyrights are property of their respective owners.
//
// Redistribution and use in source and binary forms, with or without modification,
// are permitted provided that the following conditions are met:
//
//   * Redistribution's of source code must retain the above copyright notice,
//     this list of conditions and the following disclaimer.
//
//   * Redistribution's in binary form must reproduce the above copyright notice,
//     this list of conditions and the following disclaimer in the documentation
//     and/or other materials provided with the distribution.
//
//   * The name of Intel Corporation may not be used to endorse or promote products
//     derived from this software without specific prior written permission.
//
// This software is provided by the copyright holders and contributors "as is" and
// any express or implied warranties, including, but not limited to, the implied
// warranties of merchantability and fitness for a particular purpose are disclaimed.
// In no event shall the Intel Corporation or contributors be liable for any direct,
// indirect, incidental, special, exemplary, or consequential damages
// (including, but not limited to, procurement of substitute goods or services;
// loss of use, data, or profits; or business interruption) however caused
// and on any theory of liability, whether in contract, strict liability,
// or tort (including negligence or otherwise) arising in any way out of
// the use of this software, even if advised of the possibility of such damage.
//
//M*/

#include "_cv.h"
#include "_cvmodelest.h"
#include <algorithm>
#include <limits>

using namespace std;


CvModelEstimator2::CvModelEstimator2(int _modelPoints, CvSize _modelSize, int _maxBasicSolutions)
{
    modelPoints = _modelPoints;
    modelSize = _modelSize;
    maxBasicSolutions = _maxBasicSolutions;
    checkPartialSubsets = true;
    rng = cvRNG(-1);
}

CvModelEstimator2::~CvModelEstimator2()
{
}

void CvModelEstimator2::setSeed( int64 seed )
{
    rng = cvRNG(seed);
}


int CvModelEstimator2::findInliers( const CvMat* m1, const CvMat* m2,
                                    const CvMat* model, CvMat* _err,
                                    CvMat* _mask, double threshold )
{
    int i, count = _err->rows*_err->cols, goodCount = 0;
    const float* err = _err->data.fl;
    uchar* mask = _mask->data.ptr;

    computeReprojError( m1, m2, model, _err );
    threshold *= threshold;
    for( i = 0; i < count; i++ )
        goodCount += mask[i] = err[i] <= threshold;
    return goodCount;
}


CV_IMPL int
cvRANSACUpdateNumIters( double p, double ep,
                        int model_points, int max_iters )
{
    int result = 0;
    
    CV_FUNCNAME( "cvRANSACUpdateNumIters" );

    __BEGIN__;
    
    double num, denom;
    
    if( model_points <= 0 )
        CV_ERROR( CV_StsOutOfRange, "the number of model points should be positive" );

    p = MAX(p, 0.);
    p = MIN(p, 1.);
    ep = MAX(ep, 0.);
    ep = MIN(ep, 1.);

    // avoid inf's & nan's
    num = MAX(1. - p, DBL_MIN);
    denom = 1. - pow(1. - ep,model_points);
    if( denom < DBL_MIN )
        EXIT;

    num = log(num);
    denom = log(denom);
    
    result = denom >= 0 || -num >= max_iters*(-denom) ?
        max_iters : cvRound(num/denom);

    __END__;

    return result;
}

bool CvModelEstimator2::runRANSAC( const CvMat* m1, const CvMat* m2, CvMat* model,
                                        CvMat* mask, double reprojThreshold,
                                        double confidence, int maxIters )
{
    bool result = false;
    CvMat* mask0 = mask, *tmask = 0, *t;
    CvMat* models = 0, *err = 0;
    CvMat *ms1 = 0, *ms2 = 0;

    CV_FUNCNAME( "CvModelEstimator2::estimateRansac" );

    __BEGIN__;

    int iter, niters = maxIters;
    int count = m1->rows*m1->cols, maxGoodCount = 0;
    CV_ASSERT( CV_ARE_SIZES_EQ(m1, m2) && CV_ARE_SIZES_EQ(m1, mask) );

    if( count < modelPoints )
        EXIT;

    models = cvCreateMat( modelSize.height*maxBasicSolutions, modelSize.width, CV_64FC1 );
    err = cvCreateMat( 1, count, CV_32FC1 );
    tmask = cvCreateMat( 1, count, CV_8UC1 );
    
    if( count > modelPoints )
    {
        ms1 = cvCreateMat( 1, modelPoints, m1->type );
        ms2 = cvCreateMat( 1, modelPoints, m2->type );
    }
    else
    {
        niters = 1;
        ms1 = (CvMat*)m1;
        ms2 = (CvMat*)m2;
    }

    for( iter = 0; iter < niters; iter++ )
    {
        int i, goodCount, nmodels;
        if( count > modelPoints )
        {
            bool found = getSubset( m1, m2, ms1, ms2, 300 );
            if( !found )
            {
                if( iter == 0 )
                    EXIT;
                break;
            }
        }

        nmodels = runKernel( ms1, ms2, models );
        if( nmodels <= 0 )
            continue;
        for( i = 0; i < nmodels; i++ )
        {
            CvMat model_i;
            cvGetRows( models, &model_i, i*modelSize.height, (i+1)*modelSize.height );
            goodCount = findInliers( m1, m2, &model_i, err, tmask, reprojThreshold );

            if( goodCount > MAX(maxGoodCount, modelPoints-1) )
            {
                CV_SWAP( tmask, mask, t );
                cvCopy( &model_i, model );
                maxGoodCount = goodCount;
                niters = cvRANSACUpdateNumIters( confidence,
                    (double)(count - goodCount)/count, modelPoints, niters );
            }
        }
    }

    if( maxGoodCount > 0 )
    {
        if( mask != mask0 )
        {
            CV_SWAP( tmask, mask, t );
            cvCopy( tmask, mask );
        }
        result = true;
    }

    __END__;

    if( ms1 != m1 )
        cvReleaseMat( &ms1 );
    if( ms2 != m2 )
        cvReleaseMat( &ms2 );
    cvReleaseMat( &models );
    cvReleaseMat( &err );
    cvReleaseMat( &tmask );
    return result;
}


static CV_IMPLEMENT_QSORT( icvSortDistances, int, CV_LT )

bool CvModelEstimator2::runLMeDS( const CvMat* m1, const CvMat* m2, CvMat* model,
                                  CvMat* mask, double confidence, int maxIters )
{
    const double outlierRatio = 0.45;
    bool result = false;
    CvMat* models = 0;
    CvMat *ms1 = 0, *ms2 = 0;
    CvMat* err = 0;

    CV_FUNCNAME( "CvModelEstimator2::estimateLMeDS" );

    __BEGIN__;

    int iter, niters = maxIters;
    int count = m1->rows*m1->cols;
    double minMedian = DBL_MAX, sigma;

    CV_ASSERT( CV_ARE_SIZES_EQ(m1, m2) && CV_ARE_SIZES_EQ(m1, mask) );

    if( count < modelPoints )
        EXIT;

    models = cvCreateMat( modelSize.height*maxBasicSolutions, modelSize.width, CV_64FC1 );
    err = cvCreateMat( 1, count, CV_32FC1 );
    
    if( count > modelPoints )
    {
        ms1 = cvCreateMat( 1, modelPoints, m1->type );
        ms2 = cvCreateMat( 1, modelPoints, m2->type );
    }
    else
    {
        niters = 1;
        ms1 = (CvMat*)m1;
        ms2 = (CvMat*)m2;
    }

    niters = cvRound(log(1-confidence)/log(1-pow(1-outlierRatio,(double)modelPoints)));
    niters = MIN( MAX(niters, 3), maxIters );

    for( iter = 0; iter < niters; iter++ )
    {
        int i, nmodels;
        if( count > modelPoints )
        {
            bool found = getSubset( m1, m2, ms1, ms2, 300 );
            if( !found )
            {
                if( iter == 0 )
                    EXIT;
                break;
            }
        }

        nmodels = runKernel( ms1, ms2, models );
        if( nmodels <= 0 )
            continue;
        for( i = 0; i < nmodels; i++ )
        {
            CvMat model_i;
            cvGetRows( models, &model_i, i*modelSize.height, (i+1)*modelSize.height );
            computeReprojError( m1, m2, &model_i, err );
            icvSortDistances( err->data.i, count, 0 );

            double median = count % 2 != 0 ?
                err->data.fl[count/2] : (err->data.fl[count/2-1] + err->data.fl[count/2])*0.5;

            if( median < minMedian )
            {
                minMedian = median;
                cvCopy( &model_i, model );
            }
        }
    }

    if( minMedian < DBL_MAX )
    {
        sigma = 2.5*1.4826*(1 + 5./(count - modelPoints))*sqrt(minMedian);
        sigma = MAX( sigma, FLT_EPSILON*100 );

        count = findInliers( m1, m2, model, err, mask, sigma );
        result = count >= modelPoints;
    }

    __END__;

    if( ms1 != m1 )
        cvReleaseMat( &ms1 );
    if( ms2 != m2 )
        cvReleaseMat( &ms2 );
    cvReleaseMat( &models );
    cvReleaseMat( &err );
    return result;
}


bool CvModelEstimator2::getSubset( const CvMat* m1, const CvMat* m2,
                                   CvMat* ms1, CvMat* ms2, int maxAttempts )
{
    int* idx = (int*)cvStackAlloc( modelPoints*sizeof(idx[0]) );
    int i, j, k, idx_i, iters = 0;
    int type = CV_MAT_TYPE(m1->type), elemSize = CV_ELEM_SIZE(type);
    const int *m1ptr = m1->data.i, *m2ptr = m2->data.i;
    int *ms1ptr = ms1->data.i, *ms2ptr = ms2->data.i;
    int count = m1->cols*m1->rows;

    assert( CV_IS_MAT_CONT(m1->type & m2->type) && (elemSize % sizeof(int) == 0) );
    elemSize /= sizeof(int);

    for(;;)
    {
        for( i = 0; i < modelPoints && iters < maxAttempts; iters++ )
        {
            idx[i] = idx_i = cvRandInt(&rng) % count;
            for( j = 0; j < i; j++ )
                if( idx_i == idx[j] )
                    break;
            if( j < i )
                continue;
            for( k = 0; k < elemSize; k++ )
            {
                ms1ptr[i*elemSize + k] = m1ptr[idx_i*elemSize + k];
                ms2ptr[i*elemSize + k] = m2ptr[idx_i*elemSize + k];
            }
            if( checkPartialSubsets && (!checkSubset( ms1, i+1 ) || !checkSubset( ms2, i+1 )))
                continue;
            i++;
            iters = 0;
        }
        if( !checkPartialSubsets && i == modelPoints &&
            (!checkSubset( ms1, i+1 ) || !checkSubset( ms2, i+1 )))
            continue;
        break;
    }

    return i == modelPoints;
}


bool CvModelEstimator2::checkSubset( const CvMat* m, int count )
{
    int j, k, i = count-1;
    CvPoint2D64f* ptr = (CvPoint2D64f*)m->data.ptr;

    assert( CV_MAT_TYPE(m->type) == CV_64FC2 );
    
    // check that the i-th selected point does not belong
    // to a line connecting some previously selected points
    for( j = 0; j < i; j++ )
    {
        double dx1 = ptr[j].x - ptr[i].x;
        double dy1 = ptr[j].y - ptr[i].y;
        for( k = 0; k < j; k++ )
        {
            double dx2 = ptr[k].x - ptr[i].x;
            double dy2 = ptr[k].y - ptr[i].y;
            if( fabs(dx2*dy1 - dy2*dx1) < FLT_EPSILON*(fabs(dx1) + fabs(dy1) + fabs(dx2) + fabs(dy2)))
                break;
        }
        if( k < j )
            break;
    }

    return j == i;
}


namespace cv
{

class Affine3DEstimator : public CvModelEstimator2
{
public:
    Affine3DEstimator() : CvModelEstimator2(4, cvSize(4, 3), 1) {}
    virtual int runKernel( const CvMat* m1, const CvMat* m2, CvMat* model );     
protected:
    virtual void computeReprojError( const CvMat* m1, const CvMat* m2, const CvMat* model, CvMat* error );      
    virtual bool checkSubset( const CvMat* ms1, int count );
};

}

int cv::Affine3DEstimator::runKernel( const CvMat* m1, const CvMat* m2, CvMat* model )
{    
    const Point3d* from = reinterpret_cast<const Point3d*>(m1->data.ptr);
    const Point3d* to   = reinterpret_cast<const Point3d*>(m2->data.ptr);

    Mat A(12, 12, CV_64F);
    Mat B(12, 1, CV_64F);
    A = Scalar(0.0);

    for(int i = 0; i < modelPoints; ++i)
    {
        *B.ptr<Point3d>(3*i) = to[i];

        double *aptr = A.ptr<double>(3*i);
        for(int k = 0; k < 3; ++k)
        {
            aptr[3] = 1.0;
            *reinterpret_cast<Point3d*>(aptr) = from[i];
            aptr += 16;
        }                
    }

    CvMat cvA = A;
    CvMat cvB = B;
    CvMat cvX;
    cvReshape(model, &cvX, 1, 12);
    cvSolve(&cvA, &cvB, &cvX, CV_SVD );
    
    return 1;
}

void cv::Affine3DEstimator::computeReprojError( const CvMat* m1, const CvMat* m2, const CvMat* model, CvMat* error )
{
    int count = m1->rows * m1->cols;
    const Point3d* from = reinterpret_cast<const Point3d*>(m1->data.ptr);
    const Point3d* to   = reinterpret_cast<const Point3d*>(m2->data.ptr);    
    const double* F = model->data.db;
    float* err = error->data.fl;
    
    for(int i = 0; i < count; i++ )
    {
        const Point3d& f = from[i];
        const Point3d& t = to[i];

        double a = F[0]*f.x + F[1]*f.y + F[ 2]*f.z + F[ 3] - t.x;
        double b = F[4]*f.x + F[5]*f.y + F[ 6]*f.z + F[ 7] - t.y;
        double c = F[8]*f.x + F[9]*f.y + F[10]*f.z + F[11] - t.z;

        err[i] = (float)sqrt(a*a + b*b + c*c);       
    }
}

bool cv::Affine3DEstimator::checkSubset( const CvMat* ms1, int count )
{
    CV_Assert( CV_MAT_TYPE(ms1->type) == CV_64FC3 );

    int j, k, i = count - 1;
    const Point3d* ptr = reinterpret_cast<const Point3d*>(ms1->data.ptr);    
    
    // check that the i-th selected point does not belong
    // to a line connecting some previously selected points
    
    for(j = 0; j < i; ++j)
    {
        Point3d d1 = ptr[j] - ptr[i];
        double n1 = norm(d1);
        
        for(k = 0; k < j; ++k)
        {
            Point3d d2 = ptr[k] - ptr[i];            
            double n = norm(d2) * n1;

            if (fabs(d1.dot(d2) / n) > 0.996)
                break;            
        }
        if( k < j )
            break;
    }

    return j == i;
}

int cv::estimateAffine3D(const Vector<Point3f>& from, const Vector<Point3f>& to, Mat& out, Vector<uchar>& outliers, double param1, double param2)
{
    CV_Assert( from.size() == to.size() && from.size() >= 4 );

    size_t count = from.size();

    out.create(3, 4, CV_64F); 
    outliers.resize(count);
    fill(outliers.begin(), outliers.end(), (uchar)1);

    Vector<Point3d> dFrom;
    Vector<Point3d> dTo;    

    copy(from.begin(), from.end(), back_inserter(dFrom));
    copy(to.begin(), to.end(), back_inserter(dTo));
    
    CvMat F3x4 = out;
    CvMat mask  = cvMat( 1, count, CV_8U, outliers.begin() );           
    CvMat m1 = cvMat( 1, count, CV_64FC3, dFrom.begin() );    
    CvMat m2 = cvMat( 1, count, CV_64FC3,   dTo.begin() );
    
    const double epsilon = numeric_limits<double>::epsilon();        
    param1 = param1 <= 0 ? 3 : param1;
    param2 = (param2 < epsilon) ? 0.99 : (param2 > 1 - epsilon) ? 0.99 : param2;
            
    return Affine3DEstimator().runRANSAC(&m1,& m2, &F3x4, &mask, param1, param2 );    
}
