/*
 * Copyright (c) 2014, Skybotix AG, Switzerland (info@skybotix.com)
 *
 * All rights reserved.
 *
 * Redistribution and non-commercial use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright notice, this
 * list of conditions and the following disclaimer in the documentation and/or
 * other materials provided with the distribution.
 *
 * Neither the name of the {organization} nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
#include <vi_sensor_interface.hpp>

ViSensorInterface::ViSensorInterface(uint32_t image_rate, std::string data_prefix_by_user)
    : vi_sensor_connected_(false)
{
    data_prefix_ = data_prefix_by_user;
    StartIntegratedSensor(image_rate);
}

ViSensorInterface::ViSensorInterface()
    : vi_sensor_connected_(false)
{
    data_prefix_ = "mav0/";
    StartIntegratedSensor(20);
}

ViSensorInterface::~ViSensorInterface()
{

}

void ViSensorInterface::StartIntegratedSensor(uint32_t image_rate)
{

    if (image_rate > 30)
    {
        image_rate = 30;
        std::cout << "Desired image rate is too hight, setting it to 30 Hz." << std::endl;
    }

    try
    {
        drv_.init();
    }
    catch (visensor::exceptions::ConnectionException const &ex)
    {
        std::cout << ex.what() << "\n";
        return;
    }

    // set callback for image messages
    drv_.setCameraCallback(boost::bind(&ViSensorInterface::ImageCallback, this, _1));

    drv_.startAllCameras(image_rate);
    vi_sensor_connected_ = true;

    boost::thread left_image_saver_1(boost::bind(&ViSensorInterface::saver, this, 1));  // cam_id = 1 is left cam in upside-down position
    boost::thread right_image_saver_0(boost::bind(&ViSensorInterface::saver, this, 0)); // cam_id = 0 is right cam in upside-down position

    left_image_saver_1.join();
    right_image_saver_0.join();
}

void ViSensorInterface::ImageCallback(visensor::ViFrame::Ptr frame_ptr)
{
    uint32_t camera_id = frame_ptr->camera_id;
    //push image on queue
    frameQueue[camera_id].push(frame_ptr);
}

void ViSensorInterface::ImuCallback(boost::shared_ptr<visensor::ViImuMsg> imu_ptr)
{
    if (imu_ptr->imu_id != visensor::SensorId::IMU0)
        return;
    uint32_t timeNSec = imu_ptr->timestamp;
    Eigen::Vector3d gyro(imu_ptr->gyro[0], imu_ptr->gyro[1], imu_ptr->gyro[2]);
    Eigen::Vector3d acc(imu_ptr->acc[0], imu_ptr->acc[1], imu_ptr->acc[2]);
    std::cout << "IMU Accelerometer data: " << acc[0] << "\t "<< acc[1] << "\t"<< acc[2] << "\t" << std::endl;
    //Do stuff with IMU...
}

void ViSensorInterface::worker(unsigned int cam_id)
{
    while (1)
    {
        //Popping image from queue. If no image available, we perform a blocking wait
        visensor::ViFrame::Ptr frame = frameQueue[cam_id].pop();
        uint32_t camera_id = frame->camera_id;
        cv::Mat image;
        image.create(frame->height, frame->width, CV_8UC1);
        memcpy(image.data, frame->getImageRawPtr(), frame->height * frame->width);
        //update window with image
        char winName[255];
        boost::mutex::scoped_lock lock(io_mutex_);  //lock thread as opencv does seem to have problems with multithreading
        sprintf(winName, "Camera %u", camera_id);
        cv::namedWindow(winName, CV_WINDOW_AUTOSIZE);
        cv::imshow(winName, image);
        cv::waitKey(1);
    }
}

void ViSensorInterface::saver(unsigned int cam_id)
{
    uint32_t image_counter = 0;
    std::string image_file_name;

    while (1)
    {
        // popping image from queue. If no image available, we perform a blocking wait
        visensor::ViFrame::Ptr frame = frameQueue[cam_id].pop();
        uint32_t camera_id = frame->camera_id;
        cv::Mat image;
        image.create(frame->height, frame->width, CV_8UC1);
        memcpy(image.data, frame->getImageRawPtr(), frame->height * frame->width);

        // update current image file name
        char cam_index_image_index[255];
        sprintf(cam_index_image_index, "cam%d/data/%010d.png", camera_id, image_counter);
        image_file_name = data_prefix_ + cam_index_image_index;
        std::cout << image_file_name << std::endl;
        image_counter++;

        // update window with image
        char winName[255];
        boost::mutex::scoped_lock lock(io_mutex_);  //lock thread as opencv does seem to have problems with multithreading
        sprintf(winName, "Camera %u", camera_id);
        cv::namedWindow(winName, CV_WINDOW_AUTOSIZE);
        cv::imshow(winName, image);
        cv::waitKey(1);

        // save the current image
        imwrite(image_file_name, image);
    }
}