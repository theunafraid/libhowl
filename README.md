# libhowl

Helps prevent howling. To test redirected audio install soundflower virtual audio device(kernel extension).

--------

After git clone run :

git submodule update --init --recursive

## Dependencies

* automake(build only)
* libtool(build only)
* XCode(build only, lastest)
* cairo
* fftw3
* arrayfire

## Build

`make lib` </br>
`make test`

## Testing

Output will be in test directory, the howl executable. To test run ./howl </br>

### Example :

./howl </br>
--------Input Devices-------- </br>
</br>
#0 : Built-in Microphone </br>
#1 : Soundflower (2ch) </br>
#2 : Soundflower (64ch) </br>
#3 : Hesh 3 </br>
Specify source device(microphone):</br>
0</br>
Specify capture device(soundflower):</br>
1</br>

