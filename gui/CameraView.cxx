/*ckwg +29
 * Copyright 2015 by Kitware, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 *  * Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 *  * Neither name of Kitware, Inc. nor the names of any contributors may be used
 *    to endorse or promote products derived from this software without specific
 *    prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "CameraView.h"
#include "vtkMkCamera.h"

#include <vtkCamera.h>
#include <vtkCellArray.h>
#include <vtkImageActor.h>
#include <vtkImageData.h>
#include <vtkImageReader2.h>
#include <vtkImageReader2Factory.h>
#include <vtkNew.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkRenderer.h>
#include <vtkRenderWindow.h>

#include <QtCore/QDebug>

//-----------------------------------------------------------------------------
class CameraViewPrivate
{
public:
  vtkNew<vtkRenderer> renderer;
  vtkNew<vtkRenderWindow> renderWindow;

  vtkNew<vtkImageActor> imageActor;
  vtkNew<vtkImageData> emptyImage;

  vtkNew<vtkPoints> landmarkPoints;
  vtkNew<vtkCellArray> landmarkVerts;

  double imageBounds[6];
  int imageHeight;
};

QTE_IMPLEMENT_D_FUNC(CameraView)

//-----------------------------------------------------------------------------
CameraView::CameraView(QWidget* parent, Qt::WindowFlags flags)
  : QVTKWidget(parent, flags), d_ptr(new CameraViewPrivate)
{
  QTE_D();

  // Set up ortho view
  d->renderer->GetActiveCamera()->ParallelProjectionOn();
  d->renderer->GetActiveCamera()->SetClippingRange(1.0, 3.0);
  d->renderer->GetActiveCamera()->SetPosition(0.0, 0.0, 2.0);

  // Set up render pipeline
  d->renderer->SetBackground(0, 0, 0);
  d->renderWindow->AddRenderer(d->renderer.GetPointer());
  this->SetRenderWindow(d->renderWindow.GetPointer());

  // Set up landmark actor
  vtkNew<vtkPolyData> polyData;
  vtkNew<vtkPolyDataMapper> mapper;

  polyData->SetPoints(d->landmarkPoints.GetPointer());
  polyData->SetVerts(d->landmarkVerts.GetPointer());
  mapper->SetInputData(polyData.GetPointer());

  vtkNew<vtkActor> actor;
  actor->SetMapper(mapper.GetPointer());
  actor->GetProperty()->SetPointSize(2);
  d->renderer->AddActor(actor.GetPointer());

  // Set up frame image actor
  d->renderer->AddViewProp(d->imageActor.GetPointer());
  d->imageActor->SetPosition(0.0, 0.0, -0.2);

  // Create "dummy" image data for use when we have no "real" image
  d->emptyImage->SetExtent(0, 0, 0, 0, 0, 0);
  d->emptyImage->AllocateScalars(VTK_UNSIGNED_CHAR, 1);
  d->emptyImage->SetScalarComponentFromDouble(0, 0, 0, 0, 0.0);

  this->loadImage(QString(), 0);
}

//-----------------------------------------------------------------------------
CameraView::~CameraView()
{
}

//-----------------------------------------------------------------------------
void CameraView::loadImage(QString const& path, vtkMkCamera* camera)
{
  QTE_D();

  if (path.isEmpty())
  {
    auto imageDimensions = QSize(1, 1);
    if (camera)
    {
      int w, h;
      camera->GetImageDimensions(w, h);
      imageDimensions = QSize(w, h);
    }

    // If no path given, clear current image and replace with "empty" image
    d->imageActor->SetInputData(d->emptyImage.GetPointer());

    d->imageBounds[0] = 0.0; d->imageBounds[1] = imageDimensions.width() - 1;
    d->imageBounds[2] = 0.0; d->imageBounds[3] = imageDimensions.height() - 1;
    d->imageBounds[4] = 0.0; d->imageBounds[5] = 0.0;

    d->imageHeight = imageDimensions.height();
  }
  else
  {
    // Create a reader capable of reading the image file
    auto const reader =
      vtkImageReader2Factory::CreateImageReader2(qPrintable(path));
    if (!reader)
    {
      qWarning() << "Failed to create image reader for image" << path;
      this->loadImage(QString(), camera);
      return;
    }

    // Load the image
    reader->SetFileName(qPrintable(path));
    reader->Update();

    // Set data on image actor
    d->imageActor->SetInputData(reader->GetOutput());
    d->imageActor->Update();

    d->imageActor->GetBounds(d->imageBounds);
    auto const w = d->imageBounds[1] + 1 - d->imageBounds[0];
    auto const h = d->imageBounds[3] + 1 - d->imageBounds[2];
    d->imageHeight = h;

    // Delete the reader
    reader->Delete();

    // Test for errors
    if (d->imageHeight < 2)
    {
      qWarning() << "Failed to read image" << path;
      this->loadImage(QString(), camera);
      return;
    }

    // If successful, update camera image dimensions
    if (camera)
    {
      camera->SetImageDimensions(w, h);
    }
  }

  // On success, reset view (also triggers an update)
  this->resetView();
}

//-----------------------------------------------------------------------------
void CameraView::addLandmark(int id, double x, double y)
{
  QTE_D();

  Q_UNUSED(id)

  auto const vid = d->landmarkPoints->GetNumberOfPoints();

  d->landmarkPoints->InsertNextPoint(x, d->imageHeight - y, 0.0);
  d->landmarkVerts->InsertNextCell(1);
  d->landmarkVerts->InsertCellPoint(vid);
  this->update();
}

//-----------------------------------------------------------------------------
void CameraView::clearLandmarks()
{
  QTE_D();

  d->landmarkVerts->Reset();
  d->landmarkPoints->Reset();
}

//-----------------------------------------------------------------------------
void CameraView::resetView()
{
  QTE_D();

  double renderAspect[2];
  d->renderer->GetAspect(renderAspect);

  auto const w = d->imageBounds[1] - d->imageBounds[0];
  auto const h = d->imageBounds[3] - d->imageBounds[2];
  auto const a = w / h;

  auto const s = 0.5 * h * qMax(a / renderAspect[0], 1.0);

  d->renderer->ResetCamera(d->imageBounds);
  d->renderer->GetActiveCamera()->SetParallelScale(s);

  this->update();
}
