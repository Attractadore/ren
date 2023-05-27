# Exposure

To transition from HDR luminance to SDR pixel values, an image must be properly *exposed*:

$$ L' = e \cdot L = \frac{L}{L_{max}} $$

Setting the exposure $e$ too low will result in the image being dark or *underexposed*,
while setting it too high will result in an overly bright, *overexposed* image, with many pixels' values being clipped.


3 exposure modes are supported:

* Manual exposure
* Exposure calculated from camera parameters
* Automatic exposure

## Manual exposure

Exposure $e$ is simply set to a value provided by the user.

## Camera-based exposure

There are 3 camera parameters that affect how much light reaches the camera's sensor and the final image's brightness.

The first is the *relative aperture size* $N$, in f-stops.
Since the radius of the lens's aperture $D = \frac{f}{N}$, a higher value of $N$ means a smaller aperture, with less light reaching the sensor.

The second parameter is the *shutter time* $t$, in seconds.
A higher value of $t$ means that the camera's shutter is opened for longer, and more light reaches the sensor.

The last parameter is the sensor's *sensitivity* or gain $S$, in ISO units.
Higher ISO values lead to a brighter image.

#### Luminous exposure
*Luminous exposure* $H$ is the amount of light that reaches the camera's sensor:

$$ H = \frac{q t}{N^2} L $$

$q = 0.65$ is the lens and vignetting factor.

#### Exposure value
The combination of camera parameters at a given ISO value is described by the *exposure value* $EV$, in stops:

$$ EV = \log_2 \frac{N^2}{t} $$

A higher $EV$ corresponds to *lower* exposure.

It is customary to provide exposure values at ISO 100 ($EV_{100}$).
To find the $EV$ that will result in the same exposure at another ISO value:

$$ EV_S = EV_{100} + \log_2 \frac{S}{100} = \log_2 \frac{N^2}{t} \frac{S}{100} $$

#### Exposure compensation
Additionally, *exposure compensation* can be applied to over or underexpose the final image:

$$ EV_{100}' = EV_{100} - EC $$

So a higher $EC$ value means *higher* exposure.

#### Finding the max luminance
The ISO standard defined several ways for camera manufacturers to measure a sensor's sensitivity.
One them is saturation-based sensitivity:

$$ S_{sat} = \frac{78}{H_{sat}} $$

Here, $H_{sat} = \frac{q t}{N^2} L_{max}$ is the exposure that causes the sensor to become saturated.
So it's possible to find $L_{max}$ at a given ISO value:

$$ L_{max} = \frac{78}{q} \frac{N^2}{S t} = 1.2 \cdot 2^{EV_{100}} $$

Taking $EC$ into account:

$$ L_{max} = 120 \frac{N^2}{S t} 2^{-EC} $$

## Automatic exposure

Using a light meter, it's possible to measure the average luminance $L_{avg}$ of a scene and then set camera parameters based on its value:

$$ \frac{N^2}{t} = \frac{L_{avg} S}{K} $$

$K = 12.5$ is the calibration constant for a reflected-light meter.

Once $L_{avg}$ is know, $L_{max}$ can be found:

$$ L_{max} = \frac{78}{q} \frac{L_{avg}}{K} 2^{-EC} = 9.6 L_{avg} 2^{-EC} $$

#### Implementation

However, it's not possible to use a light meter with a rendered scene.
Instead, two compute shaders passes are run.

In the first pass, a histogram of the log of the luminance values is built.

In the second pass, the average of the histogram is computed.
Bin number 0 is ignored, since it mostly contains samples whose luminance is 0.
Taking them into account skews the average luminance too much towards 0, which causes everything to be overexposed.
The average of the log of the luminance is used instead of its median since it's a smooth function.
If there are "holes" with 0 samples in the histogram,
a small change in the scene's lighting can cause the median to move from one side of a "hole" to another,
which leads to large shifts in exposure.
However, the median is a better match if you want the expose for the dominant lighting condition.
So this part could definitely use some more tweaking.

#### Automatic exposure compensation

**TODO**

One disadvantage of automatic exposure is that the scene will look the same no matter how much a scene's light source's intensity changes.
To fix this, an exposure compensation curve can be fitted.

## Temporal adaption

**TODO**

## Pre-exposure in fragment shaders

**TODO**

## Sources

https://seblagarde.files.wordpress.com/2015/07/course_notes_moving_frostbite_to_pbr_v32.pdf

https://en.wikipedia.org/wiki/Film_speed

https://en.wikipedia.org/wiki/Exposure_value

https://knarkowicz.wordpress.com/2016/01/09/automatic-exposure/
