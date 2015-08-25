# The entries in the mitk_modules list must be
# ordered according to their dependencies.

set(mitk_modules
  Core
  CommandLine
  AppUtil
  DCMTesting
  RDF
  LegacyIO
  DataTypesExt
  Overlays
  LegacyGL
  AlgorithmsExt
  MapperExt
  DICOMReader
  DICOMTesting
  Qt4Qt5TestModule
  SceneSerializationBase
  PlanarFigure
  ImageDenoising
  ImageExtraction
  ImageStatistics
  LegacyAdaptors
  SceneSerialization
  GraphAlgorithms
  Multilabel
  ContourModel
  SurfaceInterpolation
  Segmentation
  PlanarFigureSegmentation
  OpenViewCore
  QmlItems
  QtWidgets
  QtWidgetsExt
  SegmentationUI
  DiffusionImaging
  GPGPU
  OpenIGTLink
  IGTBase
  IGT
  CameraCalibration
  RigidRegistration
  RigidRegistrationUI
  DeformableRegistration
  DeformableRegistrationUI
  OpenCL
  OpenCVVideoSupport
  QtOverlays
  InputDevices
  ToFHardware
  ToFProcessing
  ToFUI
  US
  USUI
  DicomUI
  Simulation
  Remeshing
  Python
  Persistence
  OpenIGTLinkUI
  IGTUI
  VtkShaders
  DicomRT
  RTUI
  IOExt
  XNAT
  TubeGraph
  BiophotonicsHardware
  Classification
  TumorInvasionAnalysis
)

if(MITK_ENABLE_PIC_READER)
  list(APPEND mitk_modules IpPicSupportIO)
endif()
