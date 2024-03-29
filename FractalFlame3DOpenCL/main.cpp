#include <utility>
#include <CL/cl.hpp>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <iterator>
#include <ctime>

#include "main.h"
#include "rdrand.h"
#include "constants.h"
#include "buffer.h"
#include "simplePPM.h"

using namespace std;



int main(int argc, char **argv){
	//srand(time(NULL));
	srand(1);
	cl_int err;
	
	// get all the availiable platforms
	vector< cl::Platform > platformList;
	cl::Platform::get(&platformList);

	checkErr(platformList.size() != 0 ? CL_SUCCESS : -1, "cl::Platform::get");

	cout << "Platform number is: " << platformList.size() << endl;
	string platformVendor;
	platformList[0].getInfo((cl_platform_info)CL_PLATFORM_VENDOR, &platformVendor);
	cout << "Platform Vendor is " << platformVendor << endl;

	// create opencl context
	cl_context_properties cprops[3] = {
		CL_CONTEXT_PLATFORM,
		(cl_context_properties)(platformList[0])(),
		0
	};

	cl::Context context(
		CL_DEVICE_TYPE_GPU,
		cprops,
		NULL,
		NULL,
		&err);

	checkErr(err, "Context::Context()");

	pointcloud *pc_buf = new pointcloud[pc_size];
	memset(pc_buf, 0, sizeof(pointcloud)* pc_size);

	// create the front and back buffers
	cl::Buffer cl_pc_buf(
		context,
		CL_MEM_WRITE_ONLY | CL_MEM_ALLOC_HOST_PTR,
		sizeof(pointcloud)* pc_size,
		NULL,
		&err);

	checkErr(err, "Buffer::Buffer() (front buffer) ");
	// create the rand buffer
	unsigned int *rand_buf = new unsigned int[rand_buf_size];
	for (int i = 0; i < rand_buf_size; i++){
		rdrand_u32(rand_buf + i);
	}

	cl::Buffer cl_rand_buf(
		context,
		CL_MEM_READ_WRITE | CL_MEM_ALLOC_HOST_PTR,
		sizeof(unsigned int)* rand_buf_size,
		NULL,
		&err);

	checkErr(err, "Buffer::Buffer() (rand buffer) ");
	affinetransform *affs = new affinetransform[4];

	checkErr(err, "Buffer::Buffer() (affs buffer) ");
	for (int i = 0; i < 4; i++){
		rdrand_f32(&affs[i].a);
		rdrand_f32(&affs[i].b);
		rdrand_f32(&affs[i].c);
		rdrand_f32(&affs[i].d);
		rdrand_f32(&affs[i].e);
		rdrand_f32(&affs[i].f);
		rdrand_f32(&affs[i].g);
		rdrand_f32(&affs[i].h);
		rdrand_f32(&affs[i].i);
		rdrand_f32(&affs[i].j);
		rdrand_f32(&affs[i].k);
		rdrand_f32(&affs[i].l);

		rdrand_f32(&affs[i].red);
		rdrand_f32(&affs[i].gre);
		rdrand_f32(&affs[i].blu);

		affs[i].red = fabs(affs[i].red);
		affs[i].gre = fabs(affs[i].gre);
		affs[i].blu = fabs(affs[i].blu);

		float max_c = max(affs[i].red, max(affs[i].gre, affs[i].blu));

		affs[i].red /= max_c;
		affs[i].gre /= max_c;
		affs[i].blu /= max_c;

	}
	cl::Buffer cl_affs(
		context,
		CL_MEM_READ_ONLY | CL_MEM_ALLOC_HOST_PTR,
		sizeof(affinetransform)* 4,
		NULL,
		&err);

	// set up devices more?
	vector< cl::Device> devices;
	devices = context.getInfo<CL_CONTEXT_DEVICES>();
	checkErr(devices.size() > 0 ? CL_SUCCESS : -1, "devices.size() > 0");

	// load and compile the OpenCL
	ifstream kernel_file("FractalEngine.cl");
	checkErr(kernel_file.is_open() ? CL_SUCCESS : -1, "FractalEngine.cl failed to open");
	string prog(istreambuf_iterator<char>(kernel_file), (std::istreambuf_iterator<char>()));

	cl::Program::Sources source(
		1,
		make_pair(prog.c_str(), prog.length() + 1));
	cl::Program program(context, source);
	err = program.build(devices, "");
	cl::STRING_CLASS buildLog;

	checkErr(err, "Program::build()");

	// create the kernel, give it frontBuffer as a parameter
	cl::Kernel kernel(program, "mainloop", &err);
	checkErr(err, "Kernel::Kernel()");
	err = kernel.setArg(0, cl_pc_buf);
	err = kernel.setArg(1, cl_rand_buf);
	err = kernel.setArg(2, cl_affs);
	checkErr(err, "Kernel::setArg(frontBuffer)");

	// create the command queues
	cl::CommandQueue queue(context, devices[0], 0, &err);
	checkErr(err, "CommandQueue::CommandQueue()");

	cl::Event frontBufferEvent;
	queue.enqueueWriteBuffer(
		cl_pc_buf, CL_TRUE, 0, sizeof(pointcloud)* pc_size, pc_buf);

	queue.enqueueWriteBuffer(
		cl_affs, CL_TRUE, 0, sizeof(affinetransform)* 4, affs);

	HistoBuffer h(wid, hei);

	queue.enqueueWriteBuffer(
		cl_rand_buf, CL_TRUE, 0, sizeof(unsigned int)* rand_buf_size, rand_buf);

	err = queue.enqueueNDRangeKernel(
		kernel,
		cl::NullRange,
		cl::NDRange(n_kernels),
		cl::NDRange(16 * 16),
		NULL, // this would be a vector to events that must be completed before this starts! queue up the RNG here :)
		&frontBufferEvent);
	checkErr(err, "CommandQueue::enqueueNDRangeKernel()");

	frontBufferEvent.wait();
	unsigned int randinit = rand_buf[0];
	unsigned int max_a = 1;
	for (int gpuiters = 0; gpuiters < 2; gpuiters++){
		cout << gpuiters << " ";
#pragma omp parallel for
		for (int i = 0; i < rand_buf_size; i++){
			rdrand_u32(rand_buf + i);
		}

		if (randinit == rand_buf[0])
			cout << "MISTAKE" << endl;

		frontBufferEvent.wait();

		queue.enqueueWriteBuffer(
			cl_rand_buf, CL_TRUE, 0, sizeof(unsigned int)* rand_buf_size, rand_buf);

		
		err = queue.enqueueReadBuffer(
			cl_pc_buf,
			CL_TRUE, // blocking
			0,
			sizeof(pointcloud) * pc_size,
			pc_buf);

		checkErr(err, "CommandQueue::enqueueReadBuffer()");

		err = queue.enqueueNDRangeKernel(
			kernel,
			cl::NullRange,
			cl::NDRange(n_kernels),
			cl::NDRange(16 * 16),
			NULL, // this would be a vector to events that must be completed before this starts! queue up the RNG here :)
			&frontBufferEvent);

		checkErr(err, "CommandQueue::enqueueNDRangeKernel()");

		cout << "gpu part done" << endl;
		//for (int i = 0; i < points_kernel; i++)
		//	cout << (pc_buf[i].x) << endl;

		for (int i = 0; i < pc_size; i++){
			float px = pc_buf[i].x * wid / 4.0f + wid / 2;
			float py = pc_buf[i].y * hei / 4.0f + hei / 2;

			int x = (int)px;
			int y = (int)py;

			if (x >= 0 && x < wid && y >= 0 && y < hei){
				h.at(x, y).r += pc_buf[i].r;
				h.at(x, y).g += pc_buf[i].g;
				h.at(x, y).b += pc_buf[i].b;

				h.at(x, y).a++;

				max_a = max(max_a, h.at(x, y).a);
			}
		}
	}
	frontBufferEvent.wait();

	float inv_log_max_a = 1.0f / log((float)max_a);

	ColBuffer image(wid, hei);

	for (int y = 0; y < hei; y++){
		for (int x = 0; x < wid; x++){
			if (h.at(x, y).a != 0){
				float alpha = h.at(x, y).a;
				float scalar = log(alpha) * inv_log_max_a;

				float hr = h.at(x, y).r;
				float hg = h.at(x, y).g;
				float hb = h.at(x, y).b;

				float hmax = max(hr, max(hg, hb));

				hr /= hmax;
				hg /= hmax;
				hb /= hmax;

				int r = (int) abs(scalar * hr * 0xff);
				int g = (int) abs(scalar * hg * 0xff);
				int b = (int) abs(scalar * hb * 0xff);

				//cout << "red " << scalar << endl;
				image.at(x, y) = Color(r, g, b);
			}
			else {
				image.at(x, y) = Color(0, 0, 0);
			}
		}
	}

	simplePPM_write_ppm("fractal.ppm", wid, hei, &image.at(0, 0)[0]);

	cout << "done" << endl;

	exit(EXIT_SUCCESS);
}