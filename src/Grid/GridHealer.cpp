/******************************************************************************************************
 * (C) 2014 markummitchell@github.com. This file is part of Engauge Digitizer, which is released      *
 * under GNU General Public License version 2 (GPLv2) or (at your option) any later version. See file *
 * LICENSE or go to gnu.org/licenses for details. Distribution requires prior written permission.     *
 ******************************************************************************************************/

#include "DocumentModelGridRemoval.h"
#include "EngaugeAssert.h"
#include "GridHealer.h"
#include "Logger.h"
#include <QImage>
#include <qmath.h>
#include <QRgb>

// Group numbers start at this value. Each group is effectively its own pixel state
const BoundaryGroup BOUNDARY_GROUP_FIRST = 100;

GridHealer::GridHealer(const QImage &imageBefore,
                       const DocumentModelGridRemoval &modelGridRemoval) :
  m_boundaryGroupNext (BOUNDARY_GROUP_FIRST),
  m_modelGridRemoval (modelGridRemoval)
{
  LOG4CPP_INFO_S ((*mainCat)) << "GridHealer::GridHealer";

  // Prevent ambiguity between PixelState and the group numbers
  ENGAUGE_ASSERT (NUM_PIXEL_STATES  < BOUNDARY_GROUP_FIRST);

  m_pixels.resize (imageBefore.height());
  for (int row = 0; row < imageBefore.height(); row++) {
    m_pixels [row].resize (imageBefore.width());

    for (int col = 0; col < imageBefore.width(); col++) {

      QRgb rgb = imageBefore.pixel(col, row);
      if (qGray (rgb) > 128) {
        m_pixels [row] [col] = PIXEL_STATE_BACKGROUND;
      } else {
        m_pixels [row] [col] = PIXEL_STATE_FOREGROUND;
      }
    }
  }
}

void GridHealer::connectCloseGroups(QImage &imageToHeal)
{
  LOG4CPP_INFO_S ((*mainCat)) << "GridHealer::connectCloseGroups";

  double closeDistanceSquared = m_modelGridRemoval.closeDistance () * m_modelGridRemoval.closeDistance ();

  // N*(N-1)/2 search for groups that are close to each other. This was iterated using integer and
  // keys().at(integer) but that was too slow and iterator approach proved to be about 30x faster. There is
  // a constraint on the outer/inner loop that we do not want to repeat an (I,J) pair as (J,I), or allow I=J
  GroupNumberToPoint::iterator itrFrom, itrTo;
  for (itrFrom = m_groupNumberToCentroid.begin(); itrFrom != m_groupNumberToCentroid.end(); itrFrom++) {

    BoundaryGroup groupFrom = itrFrom.key();

    ENGAUGE_ASSERT (m_groupNumberToPixel.contains (groupFrom));

    QPointF posCentroidFrom = itrFrom.value();
    QPointF pixelPointFrom = m_groupNumberToPixel [groupFrom];

    for (itrTo = itrFrom + 1; itrTo != m_groupNumberToCentroid.end(); itrTo++) {

      BoundaryGroup groupTo = itrTo.key();

      ENGAUGE_ASSERT (m_groupNumberToPixel.contains (groupTo));

      QPointF posCentroidTo = itrTo.value();
      QPointF pixelPointTo = m_groupNumberToPixel [groupTo];

      QPointF separation = posCentroidFrom - posCentroidTo;
      double separationMagnitudeSquared = separation.x() * separation.x() + separation.y() * separation.y();

      if (separationMagnitudeSquared < closeDistanceSquared) {

        // Draw line from pixelPointFrom to pixelPointTo
        int count = 1 + qMax (qAbs (pixelPointFrom.x() - pixelPointTo.x()),
                              qAbs (pixelPointFrom.y() - pixelPointTo.y()));

        if (count > 1) {
          for (int index = 0; index < count; index++) {

            // Replace PIXEL_STATE_REMOVED by PIXEL_STATE_HEALED
            double s = (double) index / (double) (count - 1);
            int xCol = (int) (0.5 + (1.0 - s) * pixelPointFrom.y() + s * pixelPointTo.y());
            int yRow = (int) (0.5 + (1.0 - s) * pixelPointFrom.x() + s * pixelPointTo.x());
            m_pixels [yRow] [xCol] = PIXEL_STATE_HEALED;

            // Fill in the pixel
            imageToHeal.setPixel (QPoint (xCol,
                                          yRow),
                                  Qt::black);
          }
        }
      }
    }
  }
}

void GridHealer::erasePixel (int xCol,
                             int yRow)
{
  m_pixels [yRow] [xCol] = PIXEL_STATE_REMOVED;

  for (int rowOffset = -1; rowOffset <= 1; rowOffset++) {
    int rowSearch = yRow + rowOffset;
    if (0 <= rowSearch && rowSearch < m_pixels.count()) {

      for (int colOffset = -1; colOffset <= 1; colOffset++) {
        int colSearch = xCol + colOffset;
        if (0 <= colSearch && colSearch < m_pixels[0].count()) {

          if (m_pixels [rowSearch] [colSearch] == PIXEL_STATE_FOREGROUND) {

            m_pixels [rowSearch] [colSearch] = PIXEL_STATE_ADJACENT;

          }
        }
      }
    }
  }
}

void GridHealer::groupContiguousAdjacentPixels()
{
  LOG4CPP_INFO_S ((*mainCat)) << "GridHealer::groupContiguousAdjacentPixels";

  for (int row = 0; row < m_pixels.count(); row++) {
    for (int col = 0; col < m_pixels [0].count(); col++) {

      if (m_pixels [row] [col] == PIXEL_STATE_ADJACENT) {

        // This adjacent pixel will be grouped together with all touching adjacent pixels.
        // A centroid is calculated
        int centroidCount = 0;
        double rowCentroidSum = 0, colCentroidSum = 0;

        recursiveSearchForAdjacentPixels (m_boundaryGroupNext,
                                          row,
                                          col,
                                          centroidCount,
                                          rowCentroidSum,
                                          colCentroidSum);

        if (centroidCount > 0) {
          // Save the centroid and a representative point in hash tables that are indexed by group number
          m_groupNumberToCentroid [m_boundaryGroupNext] = QPointF (rowCentroidSum / centroidCount,
                                                                   colCentroidSum / centroidCount);
          m_groupNumberToPixel [m_boundaryGroupNext] = QPointF (row,
                                                                col);
          
          ++m_boundaryGroupNext;
        }
      }
    }
  }
}

void GridHealer::heal (QImage &imageToHeal)
{
  LOG4CPP_INFO_S ((*mainCat)) << "GridHealer::heal";

  groupContiguousAdjacentPixels ();
  connectCloseGroups (imageToHeal);
}

void GridHealer::recursiveSearchForAdjacentPixels (int boundaryGroup,
                                                   int row,
                                                   int col,
                                                   int &centroidCount,
                                                   double &rowCentroidSum,
                                                   double &colCentroidSum)
{
  ENGAUGE_ASSERT (m_pixels [row] [col] == PIXEL_STATE_ADJACENT);

  // Merge coordinates into centroid
  ++centroidCount;
  rowCentroidSum += row;
  colCentroidSum += col;

  m_pixels [row] [col] = boundaryGroup;

  for (int rowOffset = -1; rowOffset <= 1; rowOffset++) {
    int rowNeighbor = row + rowOffset;
    if (0 <= rowNeighbor && rowNeighbor < m_pixels.count()) {

      for (int colOffset = -1; colOffset <= 1; colOffset++) {
        int colNeighbor = col + colOffset;
        if (0 <= colNeighbor && colNeighbor < m_pixels[0].count()) {

          if (m_pixels [rowNeighbor] [colNeighbor] == PIXEL_STATE_ADJACENT) {

            recursiveSearchForAdjacentPixels (boundaryGroup,
                                              rowNeighbor,
                                              colNeighbor,
                                              centroidCount,
                                              rowCentroidSum,
                                              colCentroidSum);
          }
        }
      }
    }
  }
}
