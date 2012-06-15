/*
  wav2png
  
  converts audiofiles that are readable via libsndfile into pngs containing their waveforms.
  usage: wav2png audio_file.wav
  
  build: make all

  dependencies: libsndfile++, libsndfile, libpng
  
  on debian, ubuntu: apt-get install libsndfile1-dev libpng++-dev libpng12-dev

  Author: Benjamin Schulz
  email: beschulz[the a with the circle]betabugs.de
  license: GPL
  
  If you find any issues, feel free to contact me.
  
  TODO:
    - add command line options for:
      - width
      - height
      - foreground color
      - background color
      - output file name
      
    - ensure, that unicode paths are working

  and most important: enjoy and have fun :D
*/

#include <sndfile.hh>
#include <png++/png.hpp>

#include <iostream>
#include <vector>
#include <iterator>

#include "options.hpp"

template <typename T>
const T& clamp(const T& x, const T& min, const T& max)
{
  return std::max(min, std::min(max, x));
}

template <typename T> struct sample_scale
{};

template <> struct sample_scale<short>
{
  static const unsigned short value = 1 << (sizeof(short)*8-1);
};

template <> struct sample_scale<float>
{
  static const float value = 1.0f;
};

/*
  compute the waveform of the supplied audio-file and store it into out_image.
*/
void compute_waveform(
  const SndfileHandle& wav,
  png::image< png::rgba_pixel >&
  out_image,
  const png::rgba_pixel& bg_color,
  const png::rgba_pixel& fg_color
)
{
  using std::size_t;
  using std::cerr;
  using std::endl;

  const unsigned h = out_image.get_height();

  // you can change it to float or short, short was much faster for me.
  typedef short sample_type;

  //there might not be enough samples, in this case, we're using a smaller image and scale it up later.
  //std::cerr << wav.frames() << std::endl;
  png::image< png::rgba_pixel > small_image;
  bool not_enough_samples = wav.frames() < (sf_count_t)out_image.get_width();

  if (not_enough_samples)
    small_image = png::image< png::rgba_pixel >( wav.frames()>0?wav.frames():1, out_image.get_height() );

  png::image< png::rgba_pixel >& image = not_enough_samples?small_image:out_image;

  assert(image.get_width() > 0);

  //std::cout << (&image==&out_image) << " " << (&image==&small_image) << std::endl;

  int frames_per_pixel  = std::max<int>(1, wav.frames() / image.get_width());
  int samples_per_pixel = wav.channels() * frames_per_pixel;
  std::size_t progress_divisor = std::max<std::size_t>(1, image.get_width()/100);

  // temp buffer for samples from audio file
  std::vector<sample_type> block(samples_per_pixel);

  /* the processing works like this:
    for each vertical pixel in the image (x), read frames_per_pixel frames from
    the audio file and find the min and max values.
  */
  for (size_t x = 0; x < image.get_width(); ++x)
  {
    // read frames
    sf_count_t n = const_cast<SndfileHandle&>(wav).readf(&block[0], frames_per_pixel) * wav.channels();
    assert(n <= (sf_count_t)block.size());

    // find min and max
    sample_type min(0);
    sample_type max(0);
    for (int i=0; i<n; i+=wav.channels()) // only left channel
    {
      min = std::min( min, block[i] );
      max = std::max( max, block[i] );
    }

    // compute "span" from top of image to min
    // this line is a little tricky because of unsignedness
    size_t y1 = clamp<size_t>((h-(-min*h/sample_scale<sample_type>::value))/2, 0, h);
    assert(0 <= y1 && y1 <= h/2);

    // compute "span" from max to bottom of image
    size_t y2 = clamp<size_t>((h+max*h/sample_scale<sample_type>::value)/2, 0, h);
    assert(h/2 <= y2 && y2 <= h);
    
    // fill span top to min
    for(size_t y=0; y<y1;++y)
      image.set_pixel(x, y, bg_color);

    // fill span min to max
    for(size_t y=y1; y<y2;++y)
      image.set_pixel(x, y, fg_color);

    // fill span max to bottom
    for(size_t y = y2; y<h; ++y)
      image.set_pixel(x, y, bg_color);
    
    // print progress
    if ( x%(progress_divisor) == 0 )
    {
      cerr << "\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\bconverting: " << 100*x/image.get_width() << "%";
    }
  }
  
  cerr << "\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\bconverting: 100%" << endl;

  // upscale the generated image
  if (not_enough_samples)
  {
    for (std::size_t y=0; y<out_image.get_height(); ++y)
      for(std::size_t x=0; x<out_image.get_width(); ++x)
      {
        std::size_t xx = x*small_image.get_width()/out_image.get_width();
        assert( xx < out_image.get_width() );
        out_image[y][x] = small_image[y][xx];
      }
  }
}


int main(int argc, char* argv[])
{
  Options options(argc, argv);

  using std::endl;
  using std::cout;
  using std::cerr;

  // open sound file
  SndfileHandle wav(options.input_file_name);

  // handle error
  if ( wav.error() )
  {
      cerr << "Error opening audio file '" << options.input_file_name << "'" << endl;
      cerr << "Error was: '" << wav.strError() << "'" << endl; 
      return 2;
  }

  //cerr << "length: " << wav.frames() / wav.samplerate() << " seconds" << endl;

  // create image
  png::image< png::rgba_pixel > image(options.width, options.height);

  //png::rgba_pixel bg_color(0xef, 0xef, 0xef, 255);
  compute_waveform(wav, image, options.background_color, options.foreground_color);

  // write image to disk
  image.write(options.output_file_name);

  #ifdef __linux__
  // this prints info about memory usage, in my tests it was: VmRSS: 4320 kB
  /*
  {
    std::ifstream proc("/proc/self/status");
    std::copy(
      std::istreambuf_iterator<char>(proc),
      std::istreambuf_iterator<char>(),
      std::ostream_iterator<char>(std::cerr)
    );
  }*/
  #endif /* __linux__ */

  return 0;
}