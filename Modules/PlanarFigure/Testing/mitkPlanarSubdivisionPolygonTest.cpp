/*=========================================================================

Program:   Medical Imaging & Interaction Toolkit
Language:  C++
Date:      $Date: 2010-03-15 11:12:36 +0100 (Mo, 15 Mrz 2010) $
Version:   $Revision: 21745 $

Copyright (c) German Cancer Research Center, Division of Medical and
Biological Informatics. All rights reserved.
See MITKCopyright.txt or http://www.mitk.org/copyright.html for details.

This software is distributed WITHOUT ANY WARRANTY; without even
the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
PURPOSE.  See the above copyright notices for more information.

=========================================================================*/

#include "mitkTestingMacros.h"
#include "mitkPlanarSubdivisionPolygon.h"
#include "mitkPlaneGeometry.h"


class mitkPlanarSubdivisionPolygonTestClass
{

public:


static void TestPlanarSubdivisionPolygonPlacement( mitk::PlanarSubdivisionPolygon::Pointer planarSubdivisionPolygon )
{
  // Test for correct minimum number of control points in cross-mode
  MITK_TEST_CONDITION( planarSubdivisionPolygon->GetMinimumNumberOfControlPoints() == 3, "Minimum number of control points" );

  // Test for correct maximum number of control points in cross-mode
  MITK_TEST_CONDITION( planarSubdivisionPolygon->GetMaximumNumberOfControlPoints() == 1000, "Maximum number of control points" );

  // Test for correct rounds of subdivisionPoints
  MITK_TEST_CONDITION( planarSubdivisionPolygon->GetSubdivisionRounds() == 5, "Subdivision point generation depth" );

  // Test for correct tension parameter
  MITK_TEST_CONDITION( planarSubdivisionPolygon->GetTensionParameter() == 0.0625, "Tension parameter" );

  // Initial placement of planarSubdivisionPolygon
  mitk::Point2D p0;
  p0[0] = 25.0; p0[1] = 25.0;
  planarSubdivisionPolygon->PlaceFigure( p0 );

  // Add second control point
  mitk::Point2D p1;
  p1[0] = 75.0; p1[1] = 25.0;
  planarSubdivisionPolygon->SetControlPoint(1, p1 );

  // Add third control point
  mitk::Point2D p2;
  p2[0] = 75.0; p2[1] = 75.0;
  planarSubdivisionPolygon->AddControlPoint( p2 );

  // Add fourth control point
  mitk::Point2D p3;
  p3[0] = 25.0; p3[1] = 75.0;
  planarSubdivisionPolygon->AddControlPoint( p3 );

  // Test for number of control points
  MITK_TEST_CONDITION( planarSubdivisionPolygon->GetNumberOfControlPoints() == 4, "Number of control points after placement" );

  // Test if PlanarFigure is closed
  MITK_TEST_CONDITION( planarSubdivisionPolygon->IsClosed(), "Test if property 'closed' is set by default" );

  // Test for number of polylines
  const mitk::PlanarFigure::PolyLineType polyLine0 = planarSubdivisionPolygon->GetPolyLine( 0 );
  mitk::PlanarFigure::PolyLineType::const_iterator iter = polyLine0.begin();
  MITK_TEST_CONDITION( planarSubdivisionPolygon->GetPolyLinesSize() == 1, "Number of polylines after placement" );

  // Test if subdivision point count is correct
  MITK_TEST_CONDITION( polyLine0.size() == 128, "correct number of subdivision points for this depth level" );

  // Test if control points are in correct order between subdivision points
  bool correctPoint = true;
  iter = polyLine0.begin();
  advance(iter, 31);
  if( iter->Point != p0 ){ correctPoint = false; }
  advance(iter, 32);
  if( iter->Point != p1 ){ correctPoint = false; }
  advance(iter, 32);
  if( iter->Point != p2 ){ correctPoint = false; }
  advance(iter, 32);
  if( iter->Point != p3 ){ correctPoint = false; }
  MITK_TEST_CONDITION( correctPoint, "Test if control points are in correct order in polyline" );

  // Test if a picked point has the correct coordinates
  correctPoint = true;

  mitk::Point2D testPoint;
  testPoint[0] = 50.000;
  testPoint[1] = 18.750;
  iter = polyLine0.begin();
  advance(iter, 47);
  if( (iter->Point[0] - testPoint[0]) + (iter->Point[1] - testPoint[1]) > mitk::eps ){ correctPoint = false; }

  testPoint[0] = 20.96007347106933593750;
  testPoint[1] = 58.74700927734375000000;
  iter = polyLine0.begin();
  advance(iter, 10);
  if( (iter->Point[0] - testPoint[0]) + (iter->Point[1] - testPoint[1]) > mitk::eps ){ correctPoint = false; }

  testPoint[0] = 76.96900177001953125000;
  testPoint[1] = 30.05101013183593750000;
  iter = polyLine0.begin();
  advance(iter, 67);
  if( (iter->Point[0] - testPoint[0]) + (iter->Point[1] - testPoint[1]) > mitk::eps ){ correctPoint = false; }

  MITK_TEST_CONDITION( correctPoint, "Test if subdivision points are calculated correctly" )

  // Test for number of measurement features
  /*
  Does not work yet

  planarSubdivisionPolygon->EvaluateFeatures();
  MITK_TEST_CONDITION( planarSubdivisionPolygon->GetNumberOfFeatures() == 2, "Number of measurement features" );

  // Test for correct feature evaluation
  double length0 = 4 * 50.0; // circumference
  MITK_TEST_CONDITION( fabs( planarSubdivisionPolygon->GetQuantity( 0 ) - length0) < mitk::eps, "Size of longest diameter" );

  double length1 = 50.0 * 50.0 ; // area
  MITK_TEST_CONDITION( fabs( planarSubdivisionPolygon->GetQuantity( 1 ) - length1) < mitk::eps, "Size of short axis diameter" );
  */
}

static void TestPlanarSubdivisionPolygonEditing( mitk::PlanarSubdivisionPolygon::Pointer planarSubdivisionPolygon )
{
  int initialNumberOfControlPoints = planarSubdivisionPolygon->GetNumberOfControlPoints();

  mitk::Point2D pnt;
  pnt[0] = 75.0; pnt[1] = 25.0;
  planarSubdivisionPolygon->AddControlPoint( pnt);

  MITK_TEST_CONDITION( planarSubdivisionPolygon->GetNumberOfControlPoints() == initialNumberOfControlPoints+1, "A new control-point shall be added" );
  MITK_TEST_CONDITION( planarSubdivisionPolygon->GetControlPoint( planarSubdivisionPolygon->GetNumberOfControlPoints()-1 ) == pnt, "Control-point shall be added at the end." );


  planarSubdivisionPolygon->RemoveControlPoint( 3 );
  MITK_TEST_CONDITION( planarSubdivisionPolygon->GetNumberOfControlPoints() == initialNumberOfControlPoints, "A control-point has been removed" );
  MITK_TEST_CONDITION( planarSubdivisionPolygon->GetControlPoint( 3 ) == pnt, "It shall be possible to remove any control-point." );

  planarSubdivisionPolygon->RemoveControlPoint( 0 );
  planarSubdivisionPolygon->RemoveControlPoint( 0 );
  planarSubdivisionPolygon->RemoveControlPoint( 0 );
  MITK_TEST_CONDITION( planarSubdivisionPolygon->GetNumberOfControlPoints() == 3, "Control-points cannot be removed if only three points remain." );

  mitk::Point2D pnt1;
  pnt1[0] = 33.0; pnt1[1] = 33.0;
  planarSubdivisionPolygon->AddControlPoint( pnt1, 0 );
  MITK_TEST_CONDITION( planarSubdivisionPolygon->GetNumberOfControlPoints() == 4, "A control-point has been added" );
  MITK_TEST_CONDITION( planarSubdivisionPolygon->GetControlPoint( 0 ) == pnt1, "It shall be possible to insert a control-point at any position." );

}

};
/**
 * mitkplanarSubdivisionPolygonTest tests the methods and behavior of mitk::planarSubdivisionPolygon with sub-tests:
 *
 * 1. Instantiation and basic tests, including feature evaluation
 *
 */
int mitkPlanarSubdivisionPolygonTest(int /* argc */, char* /*argv*/[])
{
  // always start with this!
  MITK_TEST_BEGIN("planarSubdivisionPolygon")

  // create PlaneGeometry on which to place the planarSubdivisionPolygon
  mitk::PlaneGeometry::Pointer planeGeometry = mitk::PlaneGeometry::New();
  planeGeometry->InitializeStandardPlane( 100.0, 100.0 );

  // **************************************************************************
  // 1. Instantiation and basic tests, including feature evaluation
  mitk::PlanarSubdivisionPolygon::Pointer planarSubdivisionPolygon = mitk::PlanarSubdivisionPolygon::New();
  planarSubdivisionPolygon->SetGeometry2D( planeGeometry );

  // first test: did this work?
  MITK_TEST_CONDITION_REQUIRED( planarSubdivisionPolygon.IsNotNull(), "Testing instantiation" );

  // Test placement of planarSubdivisionPolygon by control points
  mitkPlanarSubdivisionPolygonTestClass::TestPlanarSubdivisionPolygonPlacement( planarSubdivisionPolygon );

  mitkPlanarSubdivisionPolygonTestClass::TestPlanarSubdivisionPolygonEditing( planarSubdivisionPolygon );

  // always end with this!
  MITK_TEST_END();
}
