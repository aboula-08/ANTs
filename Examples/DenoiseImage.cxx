#include "antsAllocImage.h"
#include "antsCommandLineParser.h"
#include "antsUtilities.h"

#include "ReadWriteData.h"

#include "itkAddImageFilter.h"
#include "itkAdaptiveNonLocalMeansDenoisingImageFilter.h"
#include "itkIdentityTransform.h"
#include "itkLinearInterpolateImageFunction.h"
#include "itkResampleImageFilter.h"
#include "itkShrinkImageFilter.h"
#include "itkSubtractImageFilter.h"
#include "itkTimeProbe.h"

#include "ANTsVersion.h"

namespace ants
{


// template <class TFilter>
// class CommandIterationUpdate : public itk::Command
// {
// public:
//   typedef CommandIterationUpdate  Self;
//   typedef itk::Command            Superclass;
//   typedef itk::SmartPointer<Self> Pointer;
//   itkNewMacro( Self );
// protected:
//   CommandIterationUpdate()
//   {
//   };
// public:
//
//   void Execute(itk::Object *caller, const itk::EventObject & event) ITK_OVERRIDE
//   {
//     Execute( (const itk::Object *) caller, event);
//   }
//
//   void Execute(const itk::Object * object, const itk::EventObject & event) ITK_OVERRIDE
//   {
//     const TFilter * filter =
//       dynamic_cast<const TFilter *>( object );
//
//     if( typeid( event ) != typeid( itk::IterationEvent ) )
//       {
//       return;
//       }
//     if( filter->GetElapsedIterations() == 1 )
//       {
//       std::cout << "Current level = " << filter->GetCurrentLevel() + 1
//                << std::endl;
//       }
//     std::cout << "  Iteration " << filter->GetElapsedIterations()
//              << " (of "
//              << filter->GetMaximumNumberOfIterations()[filter->GetCurrentLevel()]
//              << ").  ";
//     std::cout << " Current convergence value = "
//              << filter->GetCurrentConvergenceMeasurement()
//              << " (threshold = " << filter->GetConvergenceThreshold()
//              << ")" << std::endl;
//   }
// };

template <unsigned int ImageDimension>
int Denoise( itk::ants::CommandLineParser *parser )
{
  typedef float RealType;

  typedef itk::Image<RealType, ImageDimension> ImageType;
  typename ImageType::Pointer inputImage = ITK_NULLPTR;

  typedef itk::Image<RealType, ImageDimension> MaskImageType;
  typename MaskImageType::Pointer maskImage = ITK_NULLPTR;

  bool verbose = false;
  typename itk::ants::CommandLineParser::OptionType::Pointer verboseOption =
    parser->GetOption( "verbose" );
  if( verboseOption && verboseOption->GetNumberOfFunctions() )
    {
    verbose = parser->Convert<bool>( verboseOption->GetFunction( 0 )->GetName() );
    }

  if( verbose )
    {
    std::cout << std::endl << "Running for "
             << ImageDimension << "-dimensional images." << std::endl << std::endl;
    }

  typename itk::ants::CommandLineParser::OptionType::Pointer inputImageOption =
    parser->GetOption( "input-image" );
  if( inputImageOption && inputImageOption->GetNumberOfFunctions() )
    {
    std::string inputFile = inputImageOption->GetFunction( 0 )->GetName();
    ReadImage<ImageType>( inputImage, inputFile.c_str() );
    inputImage->Update();
    inputImage->DisconnectPipeline();
    }
  else
    {
    if( verbose )
      {
      std::cerr << "Input image not specified." << std::endl;
      }
    return EXIT_FAILURE;
    }

  typedef itk::AdaptiveNonLocalMeansDenoisingImageFilter<ImageType, ImageType> DenoiserType;
  typename DenoiserType::Pointer denoiser = DenoiserType::New();

  typedef itk::ShrinkImageFilter<ImageType, ImageType> ShrinkerType;
  typename ShrinkerType::Pointer shrinker = ShrinkerType::New();
  shrinker->SetInput( inputImage );
  shrinker->SetShrinkFactors( 1 );

  typename itk::ants::CommandLineParser::OptionType::Pointer shrinkFactorOption =
    parser->GetOption( "shrink-factor" );
  int shrinkFactor = 1;
  if( shrinkFactorOption && shrinkFactorOption->GetNumberOfFunctions() )
    {
    shrinkFactor = parser->Convert<int>( shrinkFactorOption->GetFunction( 0 )->GetName() );
    }

  if( shrinkFactor != 1 && verbose )
    {
    std::cout << "A shrink factor of > 1 doesn't seem to be working.  I'm turning off this option for now." << std::endl;
    }

  shrinker->SetShrinkFactors( shrinkFactor );
  shrinker->Update();

  denoiser->SetInput( shrinker->GetOutput() );

  typename itk::ants::CommandLineParser::OptionType::Pointer noiseModelOption =
    parser->GetOption( "noise-model" );
  std::string noiseModel( "rician" );
  if( noiseModelOption && noiseModelOption->GetNumberOfFunctions() )
    {
    noiseModel = noiseModelOption->GetFunction( 0 )->GetName();
    }
  ConvertToLowerCase( noiseModel );

  if( noiseModel == std::string( "rician" ) )
    {
    denoiser->SetUseRicianNoiseModel( true );
    }
  else
    {
    denoiser->SetUseRicianNoiseModel( false );
    }

  /**
   * The parameters below are the default parameters taken from Jose's original
   *   code.  I don't have a good handle on them so I'm hiding them from the
   *   user for now.
   */

  denoiser->SetEpsilon( 0.00001 );
  denoiser->SetMeanThreshold( 0.95 );
  denoiser->SetVarianceThreshold( 0.5 );
  denoiser->SetSmoothingFactor( 1.0 );
  denoiser->SetSmoothingVariance( 2.0 );

  typename DenoiserType::NeighborhoodRadiusType blockNeighborhoodRadius;
  typename DenoiserType::NeighborhoodRadiusType neighborhoodRadius1;
  typename DenoiserType::NeighborhoodRadiusType neighborhoodRadius2;

  neighborhoodRadius1.Fill( 1 );
  neighborhoodRadius2.Fill( 3 );
  blockNeighborhoodRadius.Fill( 1 );

  denoiser->SetNeighborhoodRadius1( neighborhoodRadius1 );
  denoiser->SetNeighborhoodRadius2( neighborhoodRadius2 );
  denoiser->SetBlockNeighborhoodRadius( blockNeighborhoodRadius );

  itk::TimeProbe timer;
  timer.Start();

//   if( verbose )
//     {
//     typedef CommandIterationUpdate<DenoiserType> CommandType;
//     typename CommandType::Pointer observer = CommandType::New();
//     denoiser->AddObserver( itk::IterationEvent(), observer );
//     }

  try
    {
    // denoiser->DebugOn();
    denoiser->Update();
    }
  catch( itk::ExceptionObject & e )
    {
    if( verbose )
      {
      std::cerr << "Exception caught: " << e << std::endl;
      }
    return EXIT_FAILURE;
    }

  if( verbose )
    {
    denoiser->Print( std::cout, 3 );
    }

  timer.Stop();
  if( verbose )
    {
    std::cout << "Elapsed time: " << timer.GetMean() << std::endl;
    }

  /**
   * output
   */
  typename itk::ants::CommandLineParser::OptionType::Pointer outputOption =
    parser->GetOption( "output" );
  if( outputOption && outputOption->GetNumberOfFunctions() )
    {
    /**
     * Get the noise image and resample to full resolution
     */
    typedef itk::SubtractImageFilter<ImageType, ImageType, ImageType> SubtracterType;
    typename SubtracterType::Pointer subtracter = SubtracterType::New();
    subtracter->SetInput1( denoiser->GetInput() );
    subtracter->SetInput2( denoiser->GetOutput() );

    typedef itk::IdentityTransform<RealType, ImageDimension> TransformType;
    typename TransformType::Pointer transform = TransformType::New();
    transform->SetIdentity();

    typedef itk::LinearInterpolateImageFunction<ImageType, RealType> LinearInterpolatorType;
    typename LinearInterpolatorType::Pointer interpolator = LinearInterpolatorType::New();
    interpolator->SetInputImage( subtracter->GetOutput() );

    typedef itk::ResampleImageFilter<ImageType, ImageType, RealType> ResamplerType;
    typename ResamplerType::Pointer resampler = ResamplerType::New();
    resampler->SetTransform( transform );
    resampler->SetInterpolator( interpolator );
    resampler->SetOutputParametersFromImage( inputImage );
    resampler->UseReferenceImageOn();
    resampler->SetInput( subtracter->GetOutput() );

    typename ImageType::Pointer noiseImage = resampler->GetOutput();
    noiseImage->Update();
    noiseImage->DisconnectPipeline();

    if( outputOption->GetFunction( 0 )->GetNumberOfParameters() > 1 )
      {
      WriteImage<ImageType>( noiseImage,  ( outputOption->GetFunction( 0 )->GetParameter( 1 ) ).c_str() );
      }

    typename SubtracterType::Pointer subtracter2 = SubtracterType::New();
    subtracter2->SetInput1( inputImage );
    subtracter2->SetInput2( noiseImage );
    subtracter2->Update();

    if( outputOption->GetFunction( 0 )->GetNumberOfParameters() == 0 )
      {
      WriteImage<ImageType>( subtracter2->GetOutput(),  ( outputOption->GetFunction( 0 )->GetName() ).c_str() );
      }
    else if( outputOption->GetFunction( 0 )->GetNumberOfParameters() > 0 )
      {
      WriteImage<ImageType>( subtracter2->GetOutput(),  ( outputOption->GetFunction( 0 )->GetParameter( 0 ) ).c_str() );
      }
    }

  return EXIT_SUCCESS;
}

void InitializeCommandLineOptions( itk::ants::CommandLineParser *parser )
{
  typedef itk::ants::CommandLineParser::OptionType OptionType;

  {
  std::string description =
      std::string( "This option forces the image to be treated as a specified-" )
      + std::string( "dimensional image.  If not specified, the program tries to " )
      + std::string( "infer the dimensionality from the input image." );
  OptionType::Pointer option = OptionType::New();
  option->SetLongName( "image-dimensionality" );
  option->SetShortName( 'd' );
  option->SetUsageOption( 0, "2/3/4" );
  option->SetDescription( description );
  parser->AddOption( option );
  }

  {
  std::string description =
    std::string( "A scalar image is expected as input for noise correction.  " );

  OptionType::Pointer option = OptionType::New();
  option->SetLongName( "input-image" );
  option->SetShortName( 'i' );
  option->SetUsageOption( 0, "inputImageFilename" );
  option->SetDescription( description );
  parser->AddOption( option );
  }

  {
  std::string description =
    std::string( "Employ a Rician or Gaussian noise model.  " );

  OptionType::Pointer option = OptionType::New();
  option->SetLongName( "noise-model" );
  option->SetShortName( 'n' );
  option->SetUsageOption( 0, "(Rician)/Gaussian" );
  option->SetDescription( description );
  parser->AddOption( option );
  }


  {
  std::string description =
    std::string( "Running noise correction on large images can be time consuming. " )
    + std::string( "To lessen computation time, the input image can be resampled. " )
    + std::string( "The shrink factor, specified as a single integer, describes " )
    + std::string( "this resampling.  Shrink factor = 1 is the default." );

  OptionType::Pointer option = OptionType::New();
  option->SetLongName( "shrink-factor" );
  option->SetShortName( 's' );
  option->SetUsageOption( 0, "(1)/2/3/..." );
  option->SetDescription( description );
  parser->AddOption( option );
  }

  {
  std::string description =
    std::string( "The output consists of the noise corrected version of the " )
    + std::string( "input image.  Optionally, one can also output the estimated " )
    + std::string( "noise image." );

  OptionType::Pointer option = OptionType::New();
  option->SetLongName( "output" );
  option->SetShortName( 'o' );
  option->SetUsageOption( 0, "correctedImage" );
  option->SetUsageOption( 1, "[correctedImage,<noiseImage>]" );
  option->SetDescription( description );
  parser->AddOption( option );
  }

  {
  std::string description = std::string( "Get Version Information." );
  OptionType::Pointer option = OptionType::New();
  option->SetLongName( "version" );
  option->SetDescription( description );
  parser->AddOption( option );
  }

  {
  std::string description = std::string( "Verbose output." );

  OptionType::Pointer option = OptionType::New();
  option->SetShortName( 'v' );
  option->SetLongName( "verbose" );
  option->SetUsageOption( 0, "(0)/1" );
  option->SetDescription( description );
  parser->AddOption( option );
  }

  {
  std::string description = std::string( "Print the help menu (short version)." );

  OptionType::Pointer option = OptionType::New();
  option->SetShortName( 'h' );
  option->SetDescription( description );
  parser->AddOption( option );
  }

  {
  std::string description = std::string( "Print the help menu." );

  OptionType::Pointer option = OptionType::New();
  option->SetLongName( "help" );
  option->SetDescription( description );
  parser->AddOption( option );
  }
}

// entry point for the library; parameter 'args' is equivalent to 'argv' in (argc,argv) of commandline parameters to
// 'main()'
int DenoiseImage( std::vector<std::string> args, std::ostream* /*out_stream = NULL */ )
{
  // put the arguments coming in as 'args' into standard (argc,argv) format;
  // 'args' doesn't have the command name as first, argument, so add it manually;
  // 'args' may have adjacent arguments concatenated into one argument,
  // which the parser should handle
  args.insert( args.begin(), "DenoiseImage" );

  int     argc = args.size();
  char* * argv = new char *[args.size() + 1];
  for( unsigned int i = 0; i < args.size(); ++i )
    {
    // allocate space for the string plus a null character
    argv[i] = new char[args[i].length() + 1];
    std::strncpy( argv[i], args[i].c_str(), args[i].length() );
    // place the null character in the end
    argv[i][args[i].length()] = '\0';
    }
  argv[argc] = ITK_NULLPTR;
  // class to automatically cleanup argv upon destruction
  class Cleanup_argv
  {
public:
    Cleanup_argv( char* * argv_, int argc_plus_one_ ) : argv( argv_ ), argc_plus_one( argc_plus_one_ )
    {
    }

    ~Cleanup_argv()
    {
      for( unsigned int i = 0; i < argc_plus_one; ++i )
        {
        delete[] argv[i];
        }
      delete[] argv;
    }

private:
    char* *      argv;
    unsigned int argc_plus_one;
  };
  Cleanup_argv cleanup_argv( argv, argc + 1 );

  // antscout->set_stream( out_stream );

  itk::ants::CommandLineParser::Pointer parser =
    itk::ants::CommandLineParser::New();

  parser->SetCommand( argv[0] );

  std::string commandDescription =
    std::string( "Denoise an image using a spatially adaptive filter originally described in " )
    + std::string( "J. V. Manjon, P. Coupe, Luis Marti-Bonmati, D. L. Collins, " )
    + std::string( "and M. Robles. Adaptive Non-Local Means Denoising of MR Images With " )
    + std::string( "Spatially Varying Noise Levels, Journal of Magnetic Resonance Imaging, " )
    + std::string( "31:192-203, June 2010." );

  parser->SetCommandDescription( commandDescription );
  InitializeCommandLineOptions( parser );

  if( parser->Parse( argc, argv ) == EXIT_FAILURE )
    {
    return EXIT_FAILURE;
    }

  if( argc == 1 )
    {
    parser->PrintMenu( std::cerr, 5, false );
    return EXIT_FAILURE;
    }
  else if( parser->GetOption( "help" )->GetFunction() && parser->Convert<bool>( parser->GetOption( "help" )->GetFunction()->GetName() ) )
    {
    parser->PrintMenu( std::cout, 5, false );
    return EXIT_SUCCESS;
    }
  else if( parser->GetOption( 'h' )->GetFunction() && parser->Convert<bool>( parser->GetOption( 'h' )->GetFunction()->GetName() ) )
    {
    parser->PrintMenu( std::cout, 5, true );
    return EXIT_SUCCESS;
    }
  // Show automatic version
  itk::ants::CommandLineParser::OptionType::Pointer versionOption = parser->GetOption( "version" );
  if( versionOption && versionOption->GetNumberOfFunctions() )
    {
    std::string versionFunction = versionOption->GetFunction( 0 )->GetName();
    ConvertToLowerCase( versionFunction );
    if( versionFunction.compare( "1" ) == 0 || versionFunction.compare( "true" ) == 0 )
      {
      //Print Version Information
      std::cout << ANTs::Version::ExtendedVersionString() << std::endl;
      return EXIT_SUCCESS;
      }
    }
  // Get dimensionality
  unsigned int dimension = 3;

  itk::ants::CommandLineParser::OptionType::Pointer dimOption =
    parser->GetOption( "image-dimensionality" );
  if( dimOption && dimOption->GetNumberOfFunctions() )
    {
    dimension = parser->Convert<unsigned int>( dimOption->GetFunction( 0 )->GetName() );
    }
  else
    {
    // Read in the first intensity image to get the image dimension.
    std::string filename;

    itk::ants::CommandLineParser::OptionType::Pointer imageOption =
      parser->GetOption( "input-image" );
    if( imageOption && imageOption->GetNumberOfFunctions() > 0 )
      {
      if( imageOption->GetFunction( 0 )->GetNumberOfParameters() > 0 )
        {
        filename = imageOption->GetFunction( 0 )->GetParameter( 0 );
        }
      else
        {
        filename = imageOption->GetFunction( 0 )->GetName();
        }
      }
    else
      {
      std::cerr << "No input images were specified.  Specify an input image"
               << " with the -i option" << std::endl;
      return EXIT_FAILURE;
      }
    itk::ImageIOBase::Pointer imageIO = itk::ImageIOFactory::CreateImageIO(
        filename.c_str(), itk::ImageIOFactory::ReadMode );
    dimension = imageIO->GetNumberOfDimensions();
    }

  switch( dimension )
    {
    case 2:
      {
      Denoise<2>( parser );
      }
      break;
    case 3:
      {
      Denoise<3>( parser );
      }
      break;
    case 4:
      {
      Denoise<4>( parser );
      }
      break;
    default:
      std::cout << "Unsupported dimension" << std::endl;
      return EXIT_FAILURE;
    }
  return EXIT_SUCCESS;
}
} // namespace ants
