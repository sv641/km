# Simple inferring app using tensorflow

There are two ways to build and run this app - in a docker container or in vagrant VM.
The application code is the same, it's just the packaging is different.

Note that we require our own custom built version of tensorflow.
The code isn't changed, we just compile it with `-D_GTHREAD_USE_RECURSIVE_MUTEX_INIT_FUNC=1`
to disable use of non-POSIX primitives that are not supported on musl.

To build this version, in `km/tools/hashicorp/build_tf` run `vagrant up`
(or to make sure it is rebuilt `vagrant up --provision`, although it takes 2 - 3 hours).
Tensorflow will be built in the virtual machine,
then the resulting tensorflow<something>.whl file will be copied into that directory.

## vagrant

Run `vagrant up` to build and start the VM with the app.
`vagrant ssh` (or configure ssh by using output of `vagrant ssh-config`) into the VM.

Currently the km code doesn't support AVX/AVX2 instructions, but the TF code requires that and we don't know how to disable that.
As a result the code only works in kkm.
To make sure kvm is not interfering `sudo rmmod kvm_intel` or `sudo rmmod kvm_amd`.
Or just `sudo ln -s /dev/kkm /dev/kontain` and make sure kkm is present.
To check, run:

```bash
/opt/kontain/bin/km -Vkvm /opt/kontain/tests/hello_test.km
```

and observe:

```txt
      ...
16:10:06.078412 km_machine_setup     568  km      Trying to open device file /dev/kontain
16:10:06.078786 km_machine_setup     578  km      Using device file /dev/kontain
16:10:06.078927 km_machine_setup     585  km      Setting vm type to VM_TYPE_KKM
      ...
```

## Docker

```bash
docker build -t test-app .
```

```bash
docker run --device /dev/kkm --name test-app --rm -it -p 5000:5000 test-app bash
```

Currently the km code doesn't support AVX/AVX2 instructions, but the TF code requires that and we don't know how to disable that.
As a result the code only works in kkm.
In the future, when km with kvm is fixed, use `--device /dev/kvm` if necessary to run kvm.

To put the km related artifact in the container, run `./p.sh test-app`.
This copies km and necessary friends into the container, and switches python to unikernel version.

## To test:

Once inside a VM (or container) with the app,
to switch python between native and KM based on use `./switch.sh` for native and `./switch.sh km` for km.

Too run the app:

```bash
python app.py
```

It takes several seconds for the application to initialize.
Message `WARNING: This is a development server. Do not use it in a production deployment.` is normal and expected,
it it simply because the app.py uses simple developer's flask configuration.

And to test, run:

```bash
curl -s -X POST -F image=@dog2.jpg 'http://localhost:5000/predict' | jq .
```

or

```bash
curl -s -X POST -F image=@dog2.jpg 'http://<VM_IP_number>:5000/predict' | jq .
```


If everything is OK, it prints something like:

```json
{
  "predictions": [
    {
      "label": "Bernese_mountain_dog",
      "probability": 0.620712161064148
    },
    {
      "label": "Appenzeller",
      "probability": 0.28114044666290283
    },
    {
      "label": "EntleBucher",
      "probability": 0.07214776426553726
    },
    {
      "label": "Border_collie",
      "probability": 0.012632192112505436
    },
    {
      "label": "Greater_Swiss_Mountain_dog",
      "probability": 0.007238826714456081
    }
  ],
  "success": true
}
```

reporting probabilities that the presented image (`dog2.jpg`) is image of the particular dog breed.