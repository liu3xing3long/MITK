/*===================================================================

The Medical Imaging Interaction Toolkit (MITK)

Copyright (c) German Cancer Research Center,
Division of Medical and Biological Informatics.
All rights reserved.

This software is distributed WITHOUT ANY WARRANTY; without
even the implied warranty of MERCHANTABILITY or FITNESS FOR
A PARTICULAR PURPOSE.

See LICENSE.txt or http://www.mitk.org for details.

===================================================================*/

#include <time.h>
#include <stdio.h>
#include <stdlib.h>

#include <omp.h>
#include "itkStreamlineTrackingFilter.h"
#include <itkImageRegionConstIterator.h>
#include <itkImageRegionConstIteratorWithIndex.h>
#include <itkImageRegionIterator.h>
#include <itkImageFileWriter.h>
#include "itkPointShell.h"
#include <itkRescaleIntensityImageFilter.h>
#include <boost/lexical_cast.hpp>
#include <TrackingHandlers/mitkTrackingHandlerOdf.h>
#include <TrackingHandlers/mitkTrackingHandlerPeaks.h>
#include <TrackingHandlers/mitkTrackingHandlerTensor.h>
#include <TrackingHandlers/mitkTrackingHandlerRandomForest.h>
#include <mitkDiffusionFunctionCollection.h>

#define _USE_MATH_DEFINES
#include <math.h>

namespace itk {


StreamlineTrackingFilter
::StreamlineTrackingFilter()
  : m_PauseTracking(false)
  , m_AbortTracking(false)
  , m_BuildFibersFinished(false)
  , m_BuildFibersReady(0)
  , m_FiberPolyData(nullptr)
  , m_Points(nullptr)
  , m_Cells(nullptr)
  , m_StoppingRegions(nullptr)
  , m_TargetRegions(nullptr)
  , m_SeedImage(nullptr)
  , m_MaskImage(nullptr)
  , m_OutputProbabilityMap(nullptr)
  , m_AngularThresholdDeg(-1)
  , m_StepSizeVox(-1)
  , m_SamplingDistanceVox(-1)
  , m_AngularThreshold(-1)
  , m_StepSize(0)
  , m_MaxLength(10000)
  , m_MinTractLength(20.0)
  , m_MaxTractLength(400.0)
  , m_SeedsPerVoxel(1)
  , m_AvoidStop(true)
  , m_RandomSampling(false)
  , m_SamplingDistance(-1)
  , m_DeflectionMod(1.0)
  , m_OnlyForwardSamples(true)
  , m_UseStopVotes(true)
  , m_NumberOfSamples(30)
  , m_NumPreviousDirections(1)
  , m_MaxNumTracts(-1)
  , m_Verbose(true)
  , m_AposterioriCurvCheck(false)
  , m_DemoMode(false)
  , m_Random(true)
  , m_UseOutputProbabilityMap(false)
  , m_CurrentTracts(0)
  , m_Progress(0)
  , m_StopTracking(false)
  , m_InterpolateMask(true)
{
  this->SetNumberOfRequiredInputs(0);
}

std::string StreamlineTrackingFilter::GetStatusText()
{
  std::string status = "Seedpoints processed: " + boost::lexical_cast<std::string>(m_Progress) + "/" + boost::lexical_cast<std::string>(m_SeedPoints.size());
  if (m_SeedPoints.size()>0)
    status += " (" + boost::lexical_cast<std::string>(100*m_Progress/m_SeedPoints.size()) + "%)";
  if (m_MaxNumTracts>0)
    status += "\nFibers accepted: " + boost::lexical_cast<std::string>(m_CurrentTracts) + "/" + boost::lexical_cast<std::string>(m_MaxNumTracts);
  else
    status += "\nFibers accepted: " + boost::lexical_cast<std::string>(m_CurrentTracts);

  return status;
}

void StreamlineTrackingFilter::BeforeTracking()
{
  m_StopTracking = false;
  m_TrackingHandler->SetRandom(m_Random);
  m_TrackingHandler->InitForTracking();
  m_FiberPolyData = PolyDataType::New();
  m_Points = vtkSmartPointer< vtkPoints >::New();
  m_Cells = vtkSmartPointer< vtkCellArray >::New();

  itk::Vector< double, 3 > imageSpacing = m_TrackingHandler->GetSpacing();

  float minSpacing;
  if(imageSpacing[0]<imageSpacing[1] && imageSpacing[0]<imageSpacing[2])
    minSpacing = imageSpacing[0];
  else if (imageSpacing[1] < imageSpacing[2])
    minSpacing = imageSpacing[1];
  else
    minSpacing = imageSpacing[2];

  if (m_StepSizeVox<mitk::eps)
    m_StepSize = 0.5*minSpacing;
  else
    m_StepSize = m_StepSizeVox*minSpacing;

  if (m_AngularThresholdDeg<0)
  {
    if  (m_StepSize/minSpacing<=1.0)
      m_AngularThreshold = std::cos( 0.5 * M_PI * m_StepSize/minSpacing );
    else
      m_AngularThreshold = std::cos( 0.5 * M_PI );
  }
  else
    m_AngularThreshold = std::cos( m_AngularThresholdDeg*M_PI/180.0 );
  m_TrackingHandler->SetAngularThreshold(m_AngularThreshold);

  if (m_SamplingDistanceVox<mitk::eps)
    m_SamplingDistance = minSpacing*0.25;
  else
    m_SamplingDistance = m_SamplingDistanceVox * minSpacing;

  m_PolyDataContainer.clear();
  for (unsigned int i=0; i<this->GetNumberOfThreads(); i++)
  {
    PolyDataType poly = PolyDataType::New();
    m_PolyDataContainer.push_back(poly);
  }

  if (m_UseOutputProbabilityMap)
  {
    m_OutputProbabilityMap = ItkDoubleImgType::New();
    m_OutputProbabilityMap->SetSpacing(imageSpacing);
    m_OutputProbabilityMap->SetOrigin(m_TrackingHandler->GetOrigin());
    m_OutputProbabilityMap->SetDirection(m_TrackingHandler->GetDirection());
    m_OutputProbabilityMap->SetRegions(m_TrackingHandler->GetLargestPossibleRegion());
    m_OutputProbabilityMap->Allocate();
    m_OutputProbabilityMap->FillBuffer(0);
  }

  m_MaskInterpolator = itk::LinearInterpolateImageFunction< ItkFloatImgType, float >::New();
  m_StopInterpolator = itk::LinearInterpolateImageFunction< ItkFloatImgType, float >::New();
  m_SeedInterpolator = itk::LinearInterpolateImageFunction< ItkFloatImgType, float >::New();
  m_TargetInterpolator = itk::LinearInterpolateImageFunction< ItkFloatImgType, float >::New();

  if (m_StoppingRegions.IsNull())
  {
    m_StoppingRegions = ItkFloatImgType::New();
    m_StoppingRegions->SetSpacing( imageSpacing );
    m_StoppingRegions->SetOrigin( m_TrackingHandler->GetOrigin() );
    m_StoppingRegions->SetDirection( m_TrackingHandler->GetDirection() );
    m_StoppingRegions->SetRegions( m_TrackingHandler->GetLargestPossibleRegion() );
    m_StoppingRegions->Allocate();
    m_StoppingRegions->FillBuffer(0);
  }
  else
    std::cout << "StreamlineTracking - Using stopping region image" << std::endl;
  m_StopInterpolator->SetInputImage(m_StoppingRegions);

  if (m_TargetRegions.IsNull())
  {
    m_TargetImageSet = false;
    m_TargetRegions = ItkFloatImgType::New();
    m_TargetRegions->SetSpacing( imageSpacing );
    m_TargetRegions->SetOrigin( m_TrackingHandler->GetOrigin() );
    m_TargetRegions->SetDirection( m_TrackingHandler->GetDirection() );
    m_TargetRegions->SetRegions( m_TrackingHandler->GetLargestPossibleRegion() );
    m_TargetRegions->Allocate();
    m_TargetRegions->FillBuffer(1);
  }
  else
  {
    m_TargetImageSet = true;
    m_TargetInterpolator->SetInputImage(m_TargetRegions);
    std::cout << "StreamlineTracking - Using target region image" << std::endl;
  }

  if (m_SeedImage.IsNull())
  {
    m_SeedImageSet = false;
    m_SeedImage = ItkFloatImgType::New();
    m_SeedImage->SetSpacing( imageSpacing );
    m_SeedImage->SetOrigin( m_TrackingHandler->GetOrigin() );
    m_SeedImage->SetDirection( m_TrackingHandler->GetDirection() );
    m_SeedImage->SetRegions( m_TrackingHandler->GetLargestPossibleRegion() );
    m_SeedImage->Allocate();
    m_SeedImage->FillBuffer(1);
  }
  else
  {
    m_SeedImageSet = true;
    std::cout << "StreamlineTracking - Using seed image" << std::endl;
  }
  m_SeedInterpolator->SetInputImage(m_SeedImage);

  if (m_MaskImage.IsNull())
  {
    // initialize mask image
    m_MaskImage = ItkFloatImgType::New();
    m_MaskImage->SetSpacing( imageSpacing );
    m_MaskImage->SetOrigin( m_TrackingHandler->GetOrigin() );
    m_MaskImage->SetDirection( m_TrackingHandler->GetDirection() );
    m_MaskImage->SetRegions( m_TrackingHandler->GetLargestPossibleRegion() );
    m_MaskImage->Allocate();
    m_MaskImage->FillBuffer(1);
  }
  else
    std::cout << "StreamlineTracking - Using mask image" << std::endl;
  m_MaskInterpolator->SetInputImage(m_MaskImage);

  if (m_SeedPoints.empty())
    GetSeedPointsFromSeedImage();

  m_BuildFibersReady = 0;
  m_BuildFibersFinished = false;
  m_Tractogram.clear();
  m_SamplingPointset = mitk::PointSet::New();
  m_AlternativePointset = mitk::PointSet::New();
  m_StopVotePointset = mitk::PointSet::New();
  m_StartTime = std::chrono::system_clock::now();

  if (m_DemoMode)
    omp_set_num_threads(1);

  if (m_TrackingHandler->GetMode()==mitk::TrackingDataHandler::MODE::DETERMINISTIC)
    std::cout << "StreamlineTracking - Mode: deterministic" << std::endl;
  else if(m_TrackingHandler->GetMode()==mitk::TrackingDataHandler::MODE::PROBABILISTIC)
    std::cout << "StreamlineTracking - Mode: probabilistic" << std::endl;
  else
    std::cout << "StreamlineTracking - Mode: ???" << std::endl;

  std::cout << "StreamlineTracking - Angular threshold: " << m_AngularThreshold << " (" << 180*std::acos( m_AngularThreshold )/M_PI << "°)" << std::endl;
  std::cout << "StreamlineTracking - Stepsize: " << m_StepSize << "mm (" << m_StepSize/minSpacing << "*vox)" << std::endl;
  std::cout << "StreamlineTracking - Seeds per voxel: " << m_SeedsPerVoxel << std::endl;
  std::cout << "StreamlineTracking - Max. tract length: " << m_MaxTractLength << "mm" << std::endl;
  std::cout << "StreamlineTracking - Min. tract length: " << m_MinTractLength << "mm" << std::endl;
  std::cout << "StreamlineTracking - Max. num. tracts: " << m_MaxNumTracts << std::endl;

  std::cout << "StreamlineTracking - Num. neighborhood samples: " << m_NumberOfSamples << std::endl;
  std::cout << "StreamlineTracking - Max. sampling distance: " << m_SamplingDistance << "mm (" << m_SamplingDistance/minSpacing << "*vox)" << std::endl;
  std::cout << "StreamlineTracking - Deflection modifier: " << m_DeflectionMod << std::endl;

  std::cout << "StreamlineTracking - Use stop votes: " << m_UseStopVotes << std::endl;
  std::cout << "StreamlineTracking - Only frontal samples: " << m_OnlyForwardSamples << std::endl;

  if (m_DemoMode)
  {
    std::cout << "StreamlineTracking - Running in demo mode";
    std::cout << "StreamlineTracking - Starting streamline tracking using 1 thread" << std::endl;
  }
  else
    std::cout << "StreamlineTracking - Starting streamline tracking using " << omp_get_max_threads() << " threads" << std::endl;
}

void StreamlineTrackingFilter::CalculateNewPosition(itk::Point<float, 3>& pos, vnl_vector_fixed<float, 3>& dir)
{
  pos[0] += dir[0]*m_StepSize;
  pos[1] += dir[1]*m_StepSize;
  pos[2] += dir[2]*m_StepSize;
}

std::vector< vnl_vector_fixed<float,3> > StreamlineTrackingFilter::CreateDirections(int NPoints)
{
  std::vector< vnl_vector_fixed<float,3> > pointshell;

  if (NPoints<2)
    return pointshell;

  std::vector< float > theta; theta.resize(NPoints);

  std::vector< float > phi; phi.resize(NPoints);

  float C = sqrt(4*M_PI);

  phi[0] = 0.0;
  phi[NPoints-1] = 0.0;

  for(int i=0; i<NPoints; i++)
  {
    theta[i] = acos(-1.0+2.0*i/(NPoints-1.0)) - M_PI / 2.0;
    if( i>0 && i<NPoints-1)
    {
      phi[i] = (phi[i-1] + C / sqrt(NPoints*(1-(-1.0+2.0*i/(NPoints-1.0))*(-1.0+2.0*i/(NPoints-1.0)))));
      // % (2*DIST_POINTSHELL_PI);
    }
  }


  for(int i=0; i<NPoints; i++)
  {
    vnl_vector_fixed<float,3> d;
    d[0] = cos(theta[i]) * cos(phi[i]);
    d[1] = cos(theta[i]) * sin(phi[i]);
    d[2] = sin(theta[i]);
    pointshell.push_back(d);
  }

  return pointshell;
}


vnl_vector_fixed<float,3> StreamlineTrackingFilter::GetNewDirection(itk::Point<float, 3> &pos, std::deque<vnl_vector_fixed<float, 3> >& olddirs, itk::Index<3> &oldIndex)
{
  if (m_DemoMode)
  {
    m_SamplingPointset->Clear();
    m_AlternativePointset->Clear();
    m_StopVotePointset->Clear();
  }
  vnl_vector_fixed<float,3> direction; direction.fill(0);

  if (mitk::imv::IsInsideMask<float>(pos, m_InterpolateMask, m_MaskInterpolator) && !mitk::imv::IsInsideMask<float>(pos, m_InterpolateMask, m_StopInterpolator))
    direction = m_TrackingHandler->ProposeDirection(pos, olddirs, oldIndex); // get direction proposal at current streamline position
  else
    return direction;

  vnl_vector_fixed<float,3> olddir = olddirs.back();
  std::vector< vnl_vector_fixed<float,3> > probeVecs = CreateDirections(m_NumberOfSamples);
  itk::Point<float, 3> sample_pos;
  int alternatives = 1;
  int stop_votes = 0;
  int possible_stop_votes = 0;
  for (unsigned int i=0; i<probeVecs.size(); i++)
  {
    vnl_vector_fixed<float,3> d;
    bool is_stop_voter = false;
    if (m_Random && m_RandomSampling)
    {
      d[0] = m_TrackingHandler->GetRandDouble(-0.5, 0.5);
      d[1] = m_TrackingHandler->GetRandDouble(-0.5, 0.5);
      d[2] = m_TrackingHandler->GetRandDouble(-0.5, 0.5);
      d.normalize();
      d *= m_TrackingHandler->GetRandDouble(0,m_SamplingDistance);
    }
    else
    {
      d = probeVecs.at(i);
      float dot = dot_product(d, olddir);
      if (m_UseStopVotes && dot>0.7)
      {
        is_stop_voter = true;
        possible_stop_votes++;
      }
      else if (m_OnlyForwardSamples && dot<0)
        continue;
      d *= m_SamplingDistance;
    }

    sample_pos[0] = pos[0] + d[0];
    sample_pos[1] = pos[1] + d[1];
    sample_pos[2] = pos[2] + d[2];

    vnl_vector_fixed<float,3> tempDir; tempDir.fill(0.0);
    if (mitk::imv::IsInsideMask<float>(sample_pos, m_InterpolateMask, m_MaskInterpolator))
      tempDir = m_TrackingHandler->ProposeDirection(sample_pos, olddirs, oldIndex); // sample neighborhood
    if (tempDir.magnitude()>mitk::eps)
    {
      direction += tempDir;

      if(m_DemoMode)
        m_SamplingPointset->InsertPoint(i, sample_pos);
    }
    else if (m_AvoidStop && olddir.magnitude()>0.5) // out of white matter
    {
      if (is_stop_voter)
        stop_votes++;
      if (m_DemoMode)
        m_StopVotePointset->InsertPoint(i, sample_pos);

      float dot = dot_product(d, olddir);
      if (dot >= 0.0) // in front of plane defined by pos and olddir
        d = -d + 2*dot*olddir; // reflect
      else
        d = -d; // invert

      // look a bit further into the other direction
      sample_pos[0] = pos[0] + d[0];
      sample_pos[1] = pos[1] + d[1];
      sample_pos[2] = pos[2] + d[2];
      alternatives++;
      vnl_vector_fixed<float,3> tempDir; tempDir.fill(0.0);
      if (mitk::imv::IsInsideMask<float>(sample_pos, m_InterpolateMask, m_MaskInterpolator))
        tempDir = m_TrackingHandler->ProposeDirection(sample_pos, olddirs, oldIndex); // sample neighborhood

      if (tempDir.magnitude()>mitk::eps)  // are we back in the white matter?
      {
        direction += d * m_DeflectionMod;         // go into the direction of the white matter
        direction += tempDir;  // go into the direction of the white matter direction at this location

        if(m_DemoMode)
          m_AlternativePointset->InsertPoint(alternatives, sample_pos);
      }
      else
      {
        if (m_DemoMode)
          m_StopVotePointset->InsertPoint(i, sample_pos);
      }
    }
    else
    {
      if (m_DemoMode)
        m_StopVotePointset->InsertPoint(i, sample_pos);

      if (is_stop_voter)
        stop_votes++;
    }
  }

  if (direction.magnitude()>0.001 && (possible_stop_votes==0 || (float)stop_votes/possible_stop_votes<0.5) )
    direction.normalize();
  else
    direction.fill(0);

  return direction;
}


float StreamlineTrackingFilter::FollowStreamline(itk::Point<float, 3> pos, vnl_vector_fixed<float,3> dir, FiberType* fib, float tractLength, bool front)
{
  vnl_vector_fixed<float,3> zero_dir; zero_dir.fill(0.0);
  std::deque< vnl_vector_fixed<float,3> > last_dirs;
  for (unsigned int i=0; i<m_NumPreviousDirections-1; i++)
    last_dirs.push_back(zero_dir);

  for (int step=0; step< m_MaxLength/2; step++)
  {
    itk::Index<3> oldIndex;
    m_TrackingHandler->WorldToIndex(pos, oldIndex);

    // get new position
    CalculateNewPosition(pos, dir);

    // is new position inside of image and mask
    if (m_AbortTracking)   // if not end streamline
    {
      return tractLength;
    }
    else    // if yes, add new point to streamline
    {
      tractLength +=  m_StepSize;
      if (front)
        fib->push_front(pos);
      else
        fib->push_back(pos);

      if (m_AposterioriCurvCheck)
      {
        int curv = CheckCurvature(fib, front);
        if (curv>0)
        {
          tractLength -= m_StepSize*curv;
          while (curv>0)
          {
            if (front)
              fib->pop_front();
            else
              fib->pop_back();
            curv--;
          }
          return tractLength;
        }
      }

      if (tractLength>m_MaxTractLength)
        return tractLength;
    }

    if (m_DemoMode && !m_UseOutputProbabilityMap) // CHECK: warum sind die samplingpunkte der streamline in der visualisierung immer einen schritt voras?
    {
#pragma omp critical
      {
        m_BuildFibersReady++;
        m_Tractogram.push_back(*fib);
        BuildFibers(true);
        m_Stop = true;

        while (m_Stop){
        }
      }
    }

    dir.normalize();
    last_dirs.push_back(dir);
    if (last_dirs.size()>m_NumPreviousDirections)
      last_dirs.pop_front();
    dir = GetNewDirection(pos, last_dirs, oldIndex);

    while (m_PauseTracking){}

    if (dir.magnitude()<0.0001)
      return tractLength;
  }
  return tractLength;
}


int StreamlineTrackingFilter::CheckCurvature(FiberType* fib, bool front)
{
  float m_Distance = 5;
  if (fib->size()<3)
    return 0;

  float dist = 0;
  std::vector< vnl_vector_fixed< float, 3 > > vectors;
  vnl_vector_fixed< float, 3 > meanV; meanV.fill(0);
  float dev = 0;

  if (front)
  {
    int c=0;
    while(dist<m_Distance && c<(int)fib->size()-1)
    {
      itk::Point<float> p1 = fib->at(c);
      itk::Point<float> p2 = fib->at(c+1);

      vnl_vector_fixed< float, 3 > v;
      v[0] = p2[0]-p1[0];
      v[1] = p2[1]-p1[1];
      v[2] = p2[2]-p1[2];
      dist += v.magnitude();
      v.normalize();
      vectors.push_back(v);
      if (c==0)
        meanV += v;
      c++;
    }
  }
  else
  {
    int c=fib->size()-1;
    while(dist<m_Distance && c>0)
    {
      itk::Point<float> p1 = fib->at(c);
      itk::Point<float> p2 = fib->at(c-1);

      vnl_vector_fixed< float, 3 > v;
      v[0] = p2[0]-p1[0];
      v[1] = p2[1]-p1[1];
      v[2] = p2[2]-p1[2];
      dist += v.magnitude();
      v.normalize();
      vectors.push_back(v);
      if (c==(int)fib->size()-1)
        meanV += v;
      c--;
    }
  }
  meanV.normalize();

  for (unsigned int c=0; c<vectors.size(); c++)
  {
    float angle = dot_product(meanV, vectors.at(c));
    if (angle>1.0)
      angle = 1.0;
    if (angle<-1.0)
      angle = -1.0;
    dev += acos(angle)*180/M_PI;
  }
  if (vectors.size()>0)
    dev /= vectors.size();

  if (dev<30)
    return 0;
  else
    return vectors.size();
}

void StreamlineTrackingFilter::GetSeedPointsFromSeedImage()
{
  MITK_INFO << "StreamlineTracking - Calculating seed points.";
  m_SeedPoints.clear();

  typedef ImageRegionConstIterator< ItkFloatImgType >     MaskIteratorType;
  MaskIteratorType    sit(m_SeedImage, m_SeedImage->GetLargestPossibleRegion());
  sit.GoToBegin();

  while (!sit.IsAtEnd())
  {
    if (sit.Value()>0)
    {
      ItkFloatImgType::IndexType index = sit.GetIndex();
      itk::ContinuousIndex<float, 3> start;
      start[0] = index[0];
      start[1] = index[1];
      start[2] = index[2];
      itk::Point<float> worldPos;
      m_SeedImage->TransformContinuousIndexToPhysicalPoint(start, worldPos);

      if ( mitk::imv::IsInsideMask<float>(worldPos, m_InterpolateMask, m_MaskInterpolator) )
      {
        m_SeedPoints.push_back(worldPos);
        for (int s = 1; s < m_SeedsPerVoxel; s++)
        {
          start[0] = index[0] + m_TrackingHandler->GetRandDouble(-0.5, 0.5);
          start[1] = index[1] + m_TrackingHandler->GetRandDouble(-0.5, 0.5);
          start[2] = index[2] + m_TrackingHandler->GetRandDouble(-0.5, 0.5);

          itk::Point<float> worldPos;
          m_SeedImage->TransformContinuousIndexToPhysicalPoint(start, worldPos);
          m_SeedPoints.push_back(worldPos);
        }
      }
    }
    ++sit;
  }
}

void StreamlineTrackingFilter::GenerateData()
{
  this->BeforeTracking();
  if (m_Random)
    std::random_shuffle(m_SeedPoints.begin(), m_SeedPoints.end());

  m_CurrentTracts = 0;
  int num_seeds = m_SeedPoints.size();
  itk::Index<3> zeroIndex; zeroIndex.Fill(0);
  m_Progress = 0;
  int i = 0;
  int print_interval = num_seeds/100;
  if (print_interval<100)
    m_Verbose=false;

#pragma omp parallel
  while (i<num_seeds && !m_StopTracking)
  {


    int temp_i = 0;
#pragma omp critical
    {
      temp_i = i;
      i++;
    }

    if (temp_i>=num_seeds || m_StopTracking)
      continue;
    else if (m_Verbose && i%print_interval==0)
#pragma omp critical
    {
      m_Progress += print_interval;
      std::cout << "                                                                                                     \r";
      if (m_MaxNumTracts>0)
        std::cout << "Tried: " << m_Progress << "/" << num_seeds << " | Accepted: " << m_CurrentTracts << "/" << m_MaxNumTracts << '\r';
      else
        std::cout << "Tried: " << m_Progress << "/" << num_seeds << " | Accepted: " << m_CurrentTracts << '\r';
      cout.flush();
    }

    const itk::Point<float> worldPos = m_SeedPoints.at(temp_i);
    FiberType fib;
    float tractLength = 0;
    unsigned int counter = 0;

    // get starting direction
    vnl_vector_fixed<float,3> dir; dir.fill(0.0);
    std::deque< vnl_vector_fixed<float,3> > olddirs;
    while (olddirs.size()<m_NumPreviousDirections)
      olddirs.push_back(dir); // start without old directions (only zero directions)

    /// START DIR
    //    vnl_vector_fixed< float, 3 > gm_start_dir;
    //    if (m_ControlGmEndings)
    //    {
    //      gm_start_dir[0] = m_GmStubs[temp_i][1][0] - m_GmStubs[temp_i][0][0];
    //      gm_start_dir[1] = m_GmStubs[temp_i][1][1] - m_GmStubs[temp_i][0][1];
    //      gm_start_dir[2] = m_GmStubs[temp_i][1][2] - m_GmStubs[temp_i][0][2];
    //      gm_start_dir.normalize();
    //      olddirs.pop_back();
    //      olddirs.push_back(gm_start_dir);
    //    }

    if (mitk::imv::IsInsideMask<float>(worldPos, m_InterpolateMask, m_MaskInterpolator))
      dir = m_TrackingHandler->ProposeDirection(worldPos, olddirs, zeroIndex);

    if (dir.magnitude()>0.0001)
    {
      /// START DIR
      //      if (m_ControlGmEndings)
      //      {
      //        float a = dot_product(gm_start_dir, dir);
      //        if (a<0)
      //          dir = -dir;
      //      }

      // forward tracking
      tractLength = FollowStreamline(worldPos, dir, &fib, 0, false);
      fib.push_front(worldPos);

      // backward tracking (only if we don't explicitely start in the GM)
      tractLength = FollowStreamline(worldPos, -dir, &fib, tractLength, true);

      counter = fib.size();

      if (tractLength>=m_MinTractLength && counter>=2)
      {
#pragma omp critical
        if ( IsValidFiber(&fib) )
        {
          if (!m_StopTracking)
          {
            if (!m_UseOutputProbabilityMap)
              m_Tractogram.push_back(fib);
            else
              FiberToProbmap(&fib);
            m_CurrentTracts++;
          }
          if (m_MaxNumTracts > 0 && m_CurrentTracts>=static_cast<unsigned int>(m_MaxNumTracts))
          {
            if (!m_StopTracking)
            {
              std::cout << "                                                                                                     \r";
              MITK_INFO << "Reconstructed maximum number of tracts (" << m_CurrentTracts << "). Stopping tractography.";
            }
            m_StopTracking = true;
          }
        }
      }

    }
  }

  this->AfterTracking();
}

bool StreamlineTrackingFilter::IsValidFiber(FiberType* fib)
{
  if (m_TargetImageSet && m_SeedImageSet)
  {
    if ( mitk::imv::IsInsideMask<float>(fib->front(), m_InterpolateMask, m_SeedInterpolator)
         && mitk::imv::IsInsideMask<float>(fib->back(), m_InterpolateMask, m_TargetInterpolator) )
      return true;
    if ( mitk::imv::IsInsideMask<float>(fib->back(), m_InterpolateMask, m_SeedInterpolator)
         && mitk::imv::IsInsideMask<float>(fib->front(), m_InterpolateMask, m_TargetInterpolator) )
      return true;
    return false;
  }
  else if (m_TargetImageSet)
  {
    if ( mitk::imv::IsInsideMask<float>(fib->front(), m_InterpolateMask, m_TargetInterpolator)
         && mitk::imv::IsInsideMask<float>(fib->back(), m_InterpolateMask, m_TargetInterpolator) )
      return true;
    return false;
  }
  return true;
}

void StreamlineTrackingFilter::FiberToProbmap(FiberType* fib)
{
  ItkDoubleImgType::IndexType last_idx; last_idx.Fill(0);
  for (auto p : *fib)
  {
    ItkDoubleImgType::IndexType idx;
    m_OutputProbabilityMap->TransformPhysicalPointToIndex(p, idx);

    if (idx != last_idx)
    {
      if (m_OutputProbabilityMap->GetLargestPossibleRegion().IsInside(idx))
        m_OutputProbabilityMap->SetPixel(idx, m_OutputProbabilityMap->GetPixel(idx)+1);
      last_idx = idx;
    }
  }
}

void StreamlineTrackingFilter::BuildFibers(bool check)
{
  if (m_BuildFibersReady<omp_get_num_threads() && check)
    return;

  m_FiberPolyData = vtkSmartPointer<vtkPolyData>::New();
  vtkSmartPointer<vtkCellArray> vNewLines = vtkSmartPointer<vtkCellArray>::New();
  vtkSmartPointer<vtkPoints> vNewPoints = vtkSmartPointer<vtkPoints>::New();

  for (unsigned int i=0; i<m_Tractogram.size(); i++)
  {
    vtkSmartPointer<vtkPolyLine> container = vtkSmartPointer<vtkPolyLine>::New();
    FiberType fib = m_Tractogram.at(i);
    for (FiberType::iterator it = fib.begin(); it!=fib.end(); ++it)
    {
      vtkIdType id = vNewPoints->InsertNextPoint((*it).GetDataPointer());
      container->GetPointIds()->InsertNextId(id);
    }
    vNewLines->InsertNextCell(container);
  }

  if (check)
    for (int i=0; i<m_BuildFibersReady; i++)
      m_Tractogram.pop_back();
  m_BuildFibersReady = 0;

  m_FiberPolyData->SetPoints(vNewPoints);
  m_FiberPolyData->SetLines(vNewLines);
  m_BuildFibersFinished = true;
}

void StreamlineTrackingFilter::AfterTracking()
{
  if (m_Verbose)
    std::cout << "                                                                                                     \r";
  if (!m_UseOutputProbabilityMap)
  {
    MITK_INFO << "Reconstructed " << m_Tractogram.size() << " fibers.";
    MITK_INFO << "Generating polydata ";
    BuildFibers(false);
  }
  else
  {
    itk::RescaleIntensityImageFilter< ItkDoubleImgType, ItkDoubleImgType >::Pointer filter = itk::RescaleIntensityImageFilter< ItkDoubleImgType, ItkDoubleImgType >::New();
    filter->SetInput(m_OutputProbabilityMap);
    filter->SetOutputMaximum(1.0);
    filter->SetOutputMinimum(0.0);
    filter->Update();
    m_OutputProbabilityMap = filter->GetOutput();
  }
  MITK_INFO << "done";

  m_EndTime = std::chrono::system_clock::now();
  std::chrono::hours   hh = std::chrono::duration_cast<std::chrono::hours>(m_EndTime - m_StartTime);
  std::chrono::minutes mm = std::chrono::duration_cast<std::chrono::minutes>(m_EndTime - m_StartTime);
  std::chrono::seconds ss = std::chrono::duration_cast<std::chrono::seconds>(m_EndTime - m_StartTime);
  mm %= 60;
  ss %= 60;
  MITK_INFO << "Tracking took " << hh.count() << "h, " << mm.count() << "m and " << ss.count() << "s";

  m_SeedPoints.clear();
}

void StreamlineTrackingFilter::SetDicomProperties(mitk::FiberBundle::Pointer fib)
{
  std::string model_code_value = "-";
  std::string model_code_meaning = "-";
  std::string algo_code_value = "-";
  std::string algo_code_meaning = "-";

  if (m_TrackingHandler->GetMode()==mitk::TrackingDataHandler::DETERMINISTIC && dynamic_cast<mitk::TrackingHandlerTensor*>(m_TrackingHandler) && !m_TrackingHandler->GetInterpolate())
  {
    algo_code_value = "sup181_ee04";
    algo_code_meaning = "FACT";
  }
  else if (m_TrackingHandler->GetMode()==mitk::TrackingDataHandler::DETERMINISTIC)
  {
    algo_code_value = "sup181_ee01";
    algo_code_meaning = "Deterministic";
  }
  else if (m_TrackingHandler->GetMode()==mitk::TrackingDataHandler::PROBABILISTIC)
  {
    algo_code_value = "sup181_ee02";
    algo_code_meaning = "Probabilistic";
  }

  if (dynamic_cast<mitk::TrackingHandlerTensor*>(m_TrackingHandler) || (dynamic_cast<mitk::TrackingHandlerOdf*>(m_TrackingHandler) && dynamic_cast<mitk::TrackingHandlerOdf*>(m_TrackingHandler)->GetIsOdfFromTensor() ) )
  {
    if ( dynamic_cast<mitk::TrackingHandlerTensor*>(m_TrackingHandler) && dynamic_cast<mitk::TrackingHandlerTensor*>(m_TrackingHandler)->GetNumTensorImages()>1 )
    {
      model_code_value = "sup181_bb02";
      model_code_meaning = "Multi Tensor";
    }
    else
    {
      model_code_value = "sup181_bb01";
      model_code_meaning = "Single Tensor";
    }
  }
  else if (dynamic_cast<mitk::TrackingHandlerRandomForest<6, 28>*>(m_TrackingHandler) || dynamic_cast<mitk::TrackingHandlerRandomForest<6, 100>*>(m_TrackingHandler))
  {
    model_code_value = "sup181_bb03";
    model_code_meaning = "Model Free";
  }
  else if (dynamic_cast<mitk::TrackingHandlerOdf*>(m_TrackingHandler))
  {
    model_code_value = "-";
    model_code_meaning = "ODF";
  }
  else if (dynamic_cast<mitk::TrackingHandlerPeaks*>(m_TrackingHandler))
  {
    model_code_value = "-";
    model_code_meaning = "Peaks";
  }

  fib->SetProperty("DICOM.anatomy.value", mitk::StringProperty::New("T-A0095"));
  fib->SetProperty("DICOM.anatomy.meaning", mitk::StringProperty::New("White matter of brain and spinal cord"));

  fib->SetProperty("DICOM.algo_code.value", mitk::StringProperty::New(algo_code_value));
  fib->SetProperty("DICOM.algo_code.meaning", mitk::StringProperty::New(algo_code_meaning));

  fib->SetProperty("DICOM.model_code.value", mitk::StringProperty::New(model_code_value));
  fib->SetProperty("DICOM.model_code.meaning", mitk::StringProperty::New(model_code_meaning));
}

}
