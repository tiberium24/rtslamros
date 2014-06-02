/**
 * \file hardwareSensorCameraRos.cpp
 * \date 20/05/2014
 * \author Ellon P. Mendes <emendes@laas.fr>
 * \ingroup rtslamros
 */

#include <algorithm>
#include <sstream>
#include <fstream>

#include "kernel/timingTools.hpp"
#include "rtslamros/hardwareSensorCameraRos.hpp"

#include <image/Image.hpp>

#include <image_transport/image_transport.h>
#include <opencv/cv.h>
#include <opencv/highgui.h>
#include <cv_bridge/cv_bridge.h>
#include <sensor_msgs/image_encodings.h>

namespace jafar {
namespace rtslamros {
namespace hardware {

using namespace jafar::rtslam;
using namespace jafar::rtslam::hardware;

void HardwareSensorCameraRos::callback(const sensor_msgs::Image& msg)
{
	cv_bridge::CvImagePtr cv_ptr;
	try
	{
		cv_ptr = cv_bridge::toCvCopy(msg, sensor_msgs::image_encodings::MONO8);
	}
	catch (cv_bridge::Exception& e)
	{
		ROS_ERROR("cv_bridge exception: %s", e.what());
		return;
	}

	(*(callback_img->img)) = cv_ptr->image;
	callback_img->timestamp = msg.header.stamp.toSec();
	callback_img->arrival = kernel::Clock::getTime();

	if (index == 0) { this->timestamps_correction = callback_img->arrival - callback_img->timestamp; }
}


void HardwareSensorCameraRos::preloadTask(void)
{ JFR_GLOBAL_TRY
	//struct timeval ts, *pts = &ts;
	int r;
	bool last_ok = true;
	//bool emptied_buffers = false;
	//double date = 0.;
	index = 0;

	std::fstream fb;
	if (mode == rtslam::hardware::mOnlineDump || mode == rtslam::hardware::mOffline)
	{
		std::ostringstream oss; oss << dump_path << "/images";
		if (id()) oss << "_" << id();
		oss << ".dat";
		fb.open(oss.str().c_str(), std::ios_base::out | std::ios_base::binary);
	}

	/// @todo Maybe change to CameraSubscriber (from image_transport)
	ros::Subscriber sub;

	bool has_publisher = false;
	// the image topic is set to "image_raw". Remap it to your topic name with
	//     image_raw:=/your/topic/name
	// in command line or
	//     <remap from="image_raw" to="/your/topic/name"/>
	// in the launch file
	if(mode != mOffline)
		sub = nh.subscribe("image_raw", 500, &HardwareSensorCameraRos::callback, this);

	while(!stopping)
	{

		// Wait until has a publisher and set the has_publisher flag once got one publisher.
		if(!has_publisher && sub.getNumPublishers() == 0) continue; else has_publisher = true;

		// Verify if the publisher finished and then finish too.
		if(has_publisher && sub.getNumPublishers() == 0 && camera_callback_queue.empty()){
			std::cout << "No more publishers. Stopping camera..." << std::endl;
			no_more_data = true; stopping = true; break;
		}

		// capture image
		int buff_write = getWritePos(true); // don't need to lock because we are the only writer
		callback_img = bufferSpecPtr[buff_write];
		if(camera_callback_queue.callOne(ros::WallDuration()) != ros::CallbackQueue::Called) continue;

		if (enabled)
		{
			boost::unique_lock<boost::mutex> l(mutex_data);
			bufferSpecPtr[buff_write]->timestamp += timestamps_correction;
			update_arrival_delay(bufferSpecPtr[buff_write]->arrival - bufferSpecPtr[buff_write]->timestamp);
			last_timestamp = bufferSpecPtr[buff_write]->timestamp;
			if (started) incWritePos(true);
			l.unlock();
			last_ok = true;
			if (condition) condition->setAndNotify(1);
		}

		if (mode == rtslam::hardware::mOnlineDump)
		{
			rtslam::rawimage_ptr_t img = rtslam::rawimage_ptr_t(static_cast<rtslam::RawImage*>(bufferSpecPtr[buff_write]->clone()));
			loggerTask->push(new LoggableImage(dump_path, img, index, (id() ? id() : -1), fb));
		}
		++index;
		//#endif
	}

	if (mode == rtslam::hardware::mOnlineDump) { loggerTask->push(new rtslam::LoggableClose(fb)); while (fb.is_open()) usleep(100000); }
	if (mode == rtslam::hardware::mOffline) fb.close();

	JFR_GLOBAL_CATCH
}

	HardwareSensorCameraRos::HardwareSensorCameraRos(kernel::VariableCondition<int> *condition, rtslam::hardware::Mode mode, int cam_id, cv::Size imgSize,
													 int bufferSize, kernel::LoggerTask *loggerTask,std::string dump_path):
		HardwareSensorCamera(condition, mode, cam_id, bufferSize, loggerTask)
	{
		HardwareSensorCamera::init(mode,dump_path,imgSize);
		nh.setCallbackQueue(&camera_callback_queue);
	}


	HardwareSensorCameraRos::~HardwareSensorCameraRos()
	{
		if (mode == rtslam::hardware::mOnline || mode == rtslam::hardware::mOnlineDump)
		{
			if (!stopping && preloadTask_thread) { started = true; stop(); join(); }
		}
	}

}}}

