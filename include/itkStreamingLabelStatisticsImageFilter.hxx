/*=========================================================================
 *
 *  Copyright Insight Software Consortium
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *         http://www.apache.org/licenses/LICENSE-2.0.txt
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 *=========================================================================*/
#ifndef itkStreamingLabelStatisticsImageFilter_hxx
#define itkStreamingLabelStatisticsImageFilter_hxx
#include "itkStreamingLabelStatisticsImageFilter.h"

#include "itkImageRegionIterator.h"
#include "itkImageRegionConstIteratorWithIndex.h"
#include "itkProgressReporter.h"

namespace itk
{
template< class TInputImage, class TLabelImage >
StreamingLabelStatisticsImageFilter< TInputImage, TLabelImage >
::StreamingLabelStatisticsImageFilter()
{
  this->SetNumberOfRequiredInputs(2);
  m_UseHistograms = false;
  m_NumBins.SetSize(1);
  m_NumBins[0] = 20;
  m_LowerBound = static_cast< RealType >( NumericTraits< PixelType >::NonpositiveMin() );
  m_UpperBound = static_cast< RealType >( NumericTraits< PixelType >::max() );
  m_ValidLabelValues.clear();

}


template< class TInputImage, class TLabelImage >
StreamingLabelStatisticsImageFilter< TInputImage, TLabelImage >
::~StreamingLabelStatisticsImageFilter()
{
}

template< class TInputImage, class TLabelImage >
void
StreamingLabelStatisticsImageFilter< TInputImage, TLabelImage >
::SetHistogramParameters(const int numBins, RealType lowerBound, RealType upperBound)
{
  m_NumBins[0] = numBins;
  m_LowerBound = lowerBound;
  m_UpperBound = upperBound;
  m_UseHistograms = true;
}

template< class TInputImage, class TLabelImage >
void
StreamingLabelStatisticsImageFilter< TInputImage, TLabelImage >
::BeforeStreamedGenerateData()
{
  itkDebugMacro( << "BeforeStreamedGenerateData()" );
  ThreadIdType numberOfThreads = this->GetNumberOfThreads();

  // Resize the thread temporaries
  m_LabelStatisticsPerThread.resize(numberOfThreads);

  // Initialize the temporaries
  for ( ThreadIdType i = 0; i < numberOfThreads; ++i )
    {
    m_LabelStatisticsPerThread[i].clear();
    }

  // Initialize the final map
  m_LabelStatistics.clear();

}

template< class TInputImage, class TLabelImage >
void
StreamingLabelStatisticsImageFilter< TInputImage, TLabelImage >
::AfterStreamedGenerateData()
{
  MapIterator      mapIt;
  MapConstIterator threadIt;
  ThreadIdType     i;
  ThreadIdType     numberOfThreads = this->GetNumberOfThreads();

  // Run through the map for each thread and accumulate the count,
  // sum, and sumofsquares
  for ( i = 0; i < numberOfThreads; i++ )
    {
    // iterate over the map for this thread
    for ( threadIt = m_LabelStatisticsPerThread[i].begin();
          threadIt != m_LabelStatisticsPerThread[i].end();
          ++threadIt )
      {
      // does this label exist in the cumulative stucture yet?
      mapIt = m_LabelStatistics.find( ( *threadIt ).first );
      if ( mapIt == m_LabelStatistics.end() )
        {
        // create a new entry
        typedef typename MapType::value_type MapValueType;
        if ( m_UseHistograms )
          {
          mapIt = m_LabelStatistics.insert( MapValueType( ( *threadIt ).first,
                                                          LabelStatistics(m_NumBins[0], m_LowerBound,
                                                                          m_UpperBound) ) ).first;
          }
        else
          {
          mapIt = m_LabelStatistics.insert( MapValueType( ( *threadIt ).first,
                                                          LabelStatistics() ) ).first;
          }
        }

      // accumulate the information from this thread
      ( *mapIt ).second.m_Count += ( *threadIt ).second.m_Count;
      ( *mapIt ).second.m_Sum += ( *threadIt ).second.m_Sum;
      ( *mapIt ).second.m_SumOfSquares += ( *threadIt ).second.m_SumOfSquares;

      if ( ( *mapIt ).second.m_Minimum > ( *threadIt ).second.m_Minimum )
        {
        ( *mapIt ).second.m_Minimum = ( *threadIt ).second.m_Minimum;
        }
      if ( ( *mapIt ).second.m_Maximum < ( *threadIt ).second.m_Maximum )
        {
        ( *mapIt ).second.m_Maximum = ( *threadIt ).second.m_Maximum;
        }

      //bounding box is min,max pairs
      int dimension = ( *mapIt ).second.m_BoundingBox.size() / 2;
      for ( int ii = 0; ii < ( dimension * 2 ); ii += 2 )
        {
        if ( ( *mapIt ).second.m_BoundingBox[ii] > ( *threadIt ).second.m_BoundingBox[ii] )
          {
          ( *mapIt ).second.m_BoundingBox[ii] = ( *threadIt ).second.m_BoundingBox[ii];
          }
        if ( ( *mapIt ).second.m_BoundingBox[ii + 1] < ( *threadIt ).second.m_BoundingBox[ii + 1] )
          {
          ( *mapIt ).second.m_BoundingBox[ii + 1] = ( *threadIt ).second.m_BoundingBox[ii + 1];
          }
        }

      // if enabled, update the histogram for this label
      if ( m_UseHistograms )
        {
        typename HistogramType::IndexType index;
        index.SetSize(1);
        for ( unsigned int bin = 0; bin < m_NumBins[0]; bin++ )
          {
          index[0] = bin;
          ( *mapIt ).second.m_Histogram->IncreaseFrequency( bin, ( *threadIt ).second.m_Histogram->GetFrequency(bin) );
          }
        }
      } // end of thread map iterator loop
    }   // end of thread loop

  // compute the remainder of the statistics
  for ( mapIt = m_LabelStatistics.begin();
        mapIt != m_LabelStatistics.end();
        ++mapIt )
    {
    // mean
    ( *mapIt ).second.m_Mean = ( *mapIt ).second.m_Sum
                               / static_cast< RealType >( ( *mapIt ).second.m_Count );

    // variance
    if ( ( *mapIt ).second.m_Count > 1 )
      {
      // unbiased estimate of variance
      LabelStatistics & ls = mapIt->second;
      const RealType    sumSquared  = ls.m_Sum * ls.m_Sum;
      const RealType    count       = static_cast< RealType >( ls.m_Count );

      ls.m_Variance = ( ls.m_SumOfSquares - sumSquared / count ) / ( count - 1.0 );
      }
    else
      {
      ( *mapIt ).second.m_Variance = NumericTraits< RealType >::Zero;
      }

    // sigma
    ( *mapIt ).second.m_Sigma = vcl_sqrt( ( *mapIt ).second.m_Variance );
    }

    {
    //Now update the cached vector of valid labels.
    m_ValidLabelValues.resize(0);
    m_ValidLabelValues.reserve(m_LabelStatistics.size());
    for ( mapIt = m_LabelStatistics.begin();
      mapIt != m_LabelStatistics.end();
      ++mapIt )
      {
      m_ValidLabelValues.push_back(mapIt->first);
      }
    }
}

template< class TInputImage, class TLabelImage >
void
StreamingLabelStatisticsImageFilter< TInputImage, TLabelImage >
::ThreadedStreamedGenerateData( const RegionType &inputRegion, ThreadIdType threadId )
{
  RealType       value;
  LabelPixelType label;

  ImageRegionConstIteratorWithIndex< TInputImage > it (this->GetInput(),
                                                       inputRegion);
  ImageRegionConstIterator< TLabelImage > labelIt (this->GetLabelInput(),
                                                   inputRegion );
  MapIterator mapIt;

  int currentIterationNumber = this->GetCurrentRequestNumber();
  int numberOfIterations = this->GetNumberOfInputRequestedRegions();

  itkDebugMacro( << "ThreadedStreamedGenerateData: processing inputRegion: " << inputRegion );


  // support progress methods/callbacks
  ProgressReporter progress( this, threadId,
                             inputRegion.GetNumberOfPixels(),
                             100,
                             float( currentIterationNumber ) / numberOfIterations,
                             1.0 / numberOfIterations
    );

  // do the work
  while ( !it.IsAtEnd() )
    {
    value = static_cast< RealType >( it.Get() );
    label = labelIt.Get();

    // is the label already in this thread?
    mapIt = m_LabelStatisticsPerThread[threadId].find(label);
    if ( mapIt == m_LabelStatisticsPerThread[threadId].end() )
      {
      // create a new statistics object
      typedef typename MapType::value_type MapValueType;
      if ( m_UseHistograms )
        {
        mapIt = m_LabelStatisticsPerThread[threadId].insert( MapValueType( label,
                                                                           LabelStatistics(m_NumBins[0], m_LowerBound,
                                                                                           m_UpperBound) ) ).first;
        }
      else
        {
        mapIt = m_LabelStatisticsPerThread[threadId].insert( MapValueType( label,
                                                                           LabelStatistics() ) ).first;
        }
      }

    // update the values for this label and this thread
    if ( value < ( *mapIt ).second.m_Minimum )
      {
      ( *mapIt ).second.m_Minimum = value;
      }
    if ( value > ( *mapIt ).second.m_Maximum )
      {
      ( *mapIt ).second.m_Maximum = value;
      }

    // bounding box is min,max pairs
    for ( unsigned int i = 0; i < ( 2 * it.GetImageDimension() ); i += 2 )
      {
      typename ImageRegionConstIteratorWithIndex< TInputImage >::IndexType index = it.GetIndex();
      if ( ( *mapIt ).second.m_BoundingBox[i] > index[i / 2] )
        {
        ( *mapIt ).second.m_BoundingBox[i] = index[i / 2];
        }
      if ( ( *mapIt ).second.m_BoundingBox[i + 1] < index[i / 2] )
        {
        ( *mapIt ).second.m_BoundingBox[i + 1] = index[i / 2];
        }
      }

    ( *mapIt ).second.m_Sum += value;
    ( *mapIt ).second.m_SumOfSquares += ( value * value );
    ( *mapIt ).second.m_Count++;

    // if enabled, update the histogram for this label
    if ( m_UseHistograms )
      {
      typename HistogramType::MeasurementVectorType meas;
      meas.SetSize(1);
      meas[0] = value;
      ( *mapIt ).second.m_Histogram->IncreaseFrequencyOfMeasurement(meas, 1);
      }

    ++it;
    ++labelIt;
    progress.CompletedPixel();
    }
}

template< class TInputImage, class TLabelImage >
typename StreamingLabelStatisticsImageFilter< TInputImage, TLabelImage >::RealType
StreamingLabelStatisticsImageFilter< TInputImage, TLabelImage >
::GetMinimum(LabelPixelType label) const
{
  MapConstIterator mapIt;

  mapIt = m_LabelStatistics.find(label);
  if ( mapIt == m_LabelStatistics.end() )
    {
    // label does not exist, return a default value
    return NumericTraits< PixelType >::max();
    }
  else
    {
    return ( *mapIt ).second.m_Minimum;
    }
}

template< class TInputImage, class TLabelImage >
typename StreamingLabelStatisticsImageFilter< TInputImage, TLabelImage >::RealType
StreamingLabelStatisticsImageFilter< TInputImage, TLabelImage >
::GetMaximum(LabelPixelType label) const
{
  MapConstIterator mapIt;

  mapIt = m_LabelStatistics.find(label);
  if ( mapIt == m_LabelStatistics.end() )
    {
    // label does not exist, return a default value
    return NumericTraits< PixelType >::NonpositiveMin();
    }
  else
    {
    return ( *mapIt ).second.m_Maximum;
    }
}

template< class TInputImage, class TLabelImage >
typename StreamingLabelStatisticsImageFilter< TInputImage, TLabelImage >::RealType
StreamingLabelStatisticsImageFilter< TInputImage, TLabelImage >
::GetMean(LabelPixelType label) const
{
  MapConstIterator mapIt;

  mapIt = m_LabelStatistics.find(label);
  if ( mapIt == m_LabelStatistics.end() )
    {
    // label does not exist, return a default value
    return NumericTraits< PixelType >::Zero;
    }
  else
    {
    return ( *mapIt ).second.m_Mean;
    }
}

template< class TInputImage, class TLabelImage >
typename StreamingLabelStatisticsImageFilter< TInputImage, TLabelImage >::RealType
StreamingLabelStatisticsImageFilter< TInputImage, TLabelImage >
::GetSum(LabelPixelType label) const
{
  MapConstIterator mapIt;

  mapIt = m_LabelStatistics.find(label);
  if ( mapIt == m_LabelStatistics.end() )
    {
    // label does not exist, return a default value
    return NumericTraits< PixelType >::Zero;
    }
  else
    {
    return ( *mapIt ).second.m_Sum;
    }
}

template< class TInputImage, class TLabelImage >
typename StreamingLabelStatisticsImageFilter< TInputImage, TLabelImage >::RealType
StreamingLabelStatisticsImageFilter< TInputImage, TLabelImage >
::GetSigma(LabelPixelType label) const
{
  MapConstIterator mapIt;

  mapIt = m_LabelStatistics.find(label);
  if ( mapIt == m_LabelStatistics.end() )
    {
    // label does not exist, return a default value
    return NumericTraits< PixelType >::Zero;
    }
  else
    {
    return ( *mapIt ).second.m_Sigma;
    }
}

template< class TInputImage, class TLabelImage >
typename StreamingLabelStatisticsImageFilter< TInputImage, TLabelImage >::RealType
StreamingLabelStatisticsImageFilter< TInputImage, TLabelImage >
::GetVariance(LabelPixelType label) const
{
  MapConstIterator mapIt;

  mapIt = m_LabelStatistics.find(label);
  if ( mapIt == m_LabelStatistics.end() )
    {
    // label does not exist, return a default value
    return NumericTraits< PixelType >::Zero;
    }
  else
    {
    return ( *mapIt ).second.m_Variance;
    }
}

template< class TInputImage, class TLabelImage >
typename StreamingLabelStatisticsImageFilter< TInputImage, TLabelImage >::BoundingBoxType
StreamingLabelStatisticsImageFilter< TInputImage, TLabelImage >
::GetBoundingBox(LabelPixelType label) const
{
  MapConstIterator mapIt;

  mapIt = m_LabelStatistics.find(label);
  if ( mapIt == m_LabelStatistics.end() )
    {
    BoundingBoxType emptyBox;
    // label does not exist, return a default value
    return emptyBox;
    }
  else
    {
    return ( *mapIt ).second.m_BoundingBox;
    }
}

template< class TInputImage, class TLabelImage >
typename StreamingLabelStatisticsImageFilter< TInputImage, TLabelImage >::RegionType
StreamingLabelStatisticsImageFilter< TInputImage, TLabelImage >
::GetRegion(LabelPixelType label) const
{
  MapConstIterator mapIt;

  mapIt = m_LabelStatistics.find(label);

  if ( mapIt == m_LabelStatistics.end() )
    {
    RegionType emptyRegion;
    // label does not exist, return a default value
    return emptyRegion;
    }
  else
    {
    BoundingBoxType bbox = this->GetBoundingBox(label);
    IndexType       index;
    SizeType        size;

    unsigned int dimension = bbox.size() / 2;

    for ( unsigned int i = 0; i < dimension; i++ )
      {
      index[i] = bbox[2 * i];
      size[i] = bbox[2 * i + 1] - bbox[2 * i] + 1;
      }
    RegionType region;
    region.SetSize(size);
    region.SetIndex(index);

    return region;
    }
}

template< class TInputImage, class TLabelImage >
typename StreamingLabelStatisticsImageFilter< TInputImage, TLabelImage >::MapSizeType
StreamingLabelStatisticsImageFilter< TInputImage, TLabelImage >
::GetCount(LabelPixelType label) const
{
  MapConstIterator mapIt;

  mapIt = m_LabelStatistics.find(label);
  if ( mapIt == m_LabelStatistics.end() )
    {
    // label does not exist, return a default value
    return 0;
    }
  else
    {
    return ( *mapIt ).second.m_Count;
    }
}

template< class TInputImage, class TLabelImage >
typename StreamingLabelStatisticsImageFilter< TInputImage, TLabelImage >::RealType
StreamingLabelStatisticsImageFilter< TInputImage, TLabelImage >
::GetMedian(LabelPixelType label) const
{
  RealType         median = 0.0;
  MapConstIterator mapIt;

  mapIt = m_LabelStatistics.find(label);
  if ( mapIt == m_LabelStatistics.end() || !m_UseHistograms )
    {
    // label does not exist OR histograms not enabled, return a default value
    return median;
    }
  else
    {
    typename HistogramType::SizeValueType bin = 0;

    typename HistogramType::IndexType index;
    index.SetSize(1);
    RealType total = 0;

    // count bins until just over half the distribution is counted
    while ( total <= ( ( *mapIt ).second.m_Count / 2 ) && ( bin < m_NumBins[0] ) )
      {
      index[0] = bin;
      total += ( *mapIt ).second.m_Histogram->GetFrequency(index);
      bin++;
      }
    bin--;
    index[0] = bin;

    // return center of bin range
    RealType lowRange = ( *mapIt ).second.m_Histogram->GetBinMin(0, bin);
    RealType highRange  = ( *mapIt ).second.m_Histogram->GetBinMax(0, bin);
    median = lowRange + ( highRange - lowRange ) / 2;
    return median;
    }
}

template< class TInputImage, class TLabelImage >
typename StreamingLabelStatisticsImageFilter< TInputImage, TLabelImage >::HistogramPointer
StreamingLabelStatisticsImageFilter< TInputImage, TLabelImage >
::GetHistogram(LabelPixelType label) const
{
  MapConstIterator mapIt;

  mapIt = m_LabelStatistics.find(label);
  if ( mapIt == m_LabelStatistics.end() )
    {
    // label does not exist, return a default value
    return 0;
    }
  else
    {
    // this will be zero if histograms have not been enabled
    return ( *mapIt ).second.m_Histogram;
    }
}

template< class TImage, class TLabelImage >
void
StreamingLabelStatisticsImageFilter< TImage, TLabelImage >
::PrintSelf(std::ostream & os, Indent indent) const
{
  Superclass::PrintSelf(os, indent);

  os << indent << "Number of labels: " << m_LabelStatistics.size()
     << std::endl;
  os << indent << "Use Histograms: " << m_UseHistograms
     << std::endl;
  os << indent << "Histogram Lower Bound: " << m_LowerBound
     << std::endl;
  os << indent << "Histogram Upper Bound: " << m_UpperBound
     << std::endl;
}
} // end namespace itk
#endif
