/*
 *  Copyright 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <string>
#include <vector>

#include "webrtc/api/remotevideocapturer.h"
#include "webrtc/api/test/fakeconstraints.h"
#include "webrtc/api/videosource.h"
#include "webrtc/base/gunit.h"
#include "webrtc/media/base/fakemediaengine.h"
#include "webrtc/media/base/fakevideocapturer.h"
#include "webrtc/media/base/fakevideorenderer.h"
#include "webrtc/media/engine/webrtcvideoframe.h"
#include "webrtc/pc/channelmanager.h"

using webrtc::FakeConstraints;
using webrtc::VideoSource;
using webrtc::MediaConstraintsInterface;
using webrtc::MediaSourceInterface;
using webrtc::ObserverInterface;
using webrtc::VideoSourceInterface;

namespace {

// Max wait time for a test.
const int kMaxWaitMs = 100;

}  // anonymous namespace


// TestVideoCapturer extends cricket::FakeVideoCapturer so it can be used for
// testing without known camera formats.
// It keeps its own lists of cricket::VideoFormats for the unit tests in this
// file.
class TestVideoCapturer : public cricket::FakeVideoCapturer {
 public:
  TestVideoCapturer() : test_without_formats_(false) {
    std::vector<cricket::VideoFormat> formats;
    formats.push_back(cricket::VideoFormat(1280, 720,
        cricket::VideoFormat::FpsToInterval(30), cricket::FOURCC_I420));
    formats.push_back(cricket::VideoFormat(640, 480,
        cricket::VideoFormat::FpsToInterval(30), cricket::FOURCC_I420));
    formats.push_back(cricket::VideoFormat(640, 400,
            cricket::VideoFormat::FpsToInterval(30), cricket::FOURCC_I420));
    formats.push_back(cricket::VideoFormat(320, 240,
        cricket::VideoFormat::FpsToInterval(30), cricket::FOURCC_I420));
    formats.push_back(cricket::VideoFormat(352, 288,
            cricket::VideoFormat::FpsToInterval(30), cricket::FOURCC_I420));
    ResetSupportedFormats(formats);
  }

  // This function is used for resetting the supported capture formats and
  // simulating a cricket::VideoCapturer implementation that don't support
  // capture format enumeration. This is used to simulate the current
  // Chrome implementation.
  void TestWithoutCameraFormats() {
    test_without_formats_ = true;
    std::vector<cricket::VideoFormat> formats;
    ResetSupportedFormats(formats);
  }

  virtual cricket::CaptureState Start(
      const cricket::VideoFormat& capture_format) {
    if (test_without_formats_) {
      std::vector<cricket::VideoFormat> formats;
      formats.push_back(capture_format);
      ResetSupportedFormats(formats);
    }
    return FakeVideoCapturer::Start(capture_format);
  }

  virtual bool GetBestCaptureFormat(const cricket::VideoFormat& desired,
                                    cricket::VideoFormat* best_format) {
    if (test_without_formats_) {
      *best_format = desired;
      return true;
    }
    return FakeVideoCapturer::GetBestCaptureFormat(desired,
                                                   best_format);
  }

 private:
  bool test_without_formats_;
};

class StateObserver : public ObserverInterface {
 public:
  explicit StateObserver(VideoSourceInterface* source)
     : state_(source->state()),
       source_(source) {
  }
  virtual void OnChanged() {
    state_ = source_->state();
  }
  MediaSourceInterface::SourceState state() const { return state_; }

 private:
  MediaSourceInterface::SourceState state_;
  rtc::scoped_refptr<VideoSourceInterface> source_;
};

class VideoSourceTest : public testing::Test {
 protected:
  VideoSourceTest()
      : capturer_cleanup_(new TestVideoCapturer()),
        capturer_(capturer_cleanup_.get()),
        channel_manager_(new cricket::ChannelManager(
          new cricket::FakeMediaEngine(), rtc::Thread::Current())) {
  }

  void SetUp() {
    ASSERT_TRUE(channel_manager_->Init());
  }

  void CreateVideoSource() {
    CreateVideoSource(NULL);
  }

  void CreateVideoSource(
      const webrtc::MediaConstraintsInterface* constraints) {
    // VideoSource take ownership of |capturer_|
    source_ =
        VideoSource::Create(channel_manager_.get(), capturer_cleanup_.release(),
                            constraints, false);

    ASSERT_TRUE(source_.get() != NULL);
    EXPECT_EQ(capturer_, source_->GetVideoCapturer());

    state_observer_.reset(new StateObserver(source_));
    source_->RegisterObserver(state_observer_.get());
    source_->AddSink(&renderer_);
  }

  rtc::scoped_ptr<TestVideoCapturer> capturer_cleanup_;
  TestVideoCapturer* capturer_;
  cricket::FakeVideoRenderer renderer_;
  rtc::scoped_ptr<cricket::ChannelManager> channel_manager_;
  rtc::scoped_ptr<StateObserver> state_observer_;
  rtc::scoped_refptr<VideoSource> source_;
};


// Test that a VideoSource transition to kLive state when the capture
// device have started and kEnded if it is stopped.
// It also test that an output can receive video frames.
TEST_F(VideoSourceTest, CapturerStartStop) {
  // Initialize without constraints.
  CreateVideoSource();
  EXPECT_EQ_WAIT(MediaSourceInterface::kLive, state_observer_->state(),
                 kMaxWaitMs);

  ASSERT_TRUE(capturer_->CaptureFrame());
  EXPECT_EQ(1, renderer_.num_rendered_frames());

  capturer_->Stop();
  EXPECT_EQ_WAIT(MediaSourceInterface::kEnded, state_observer_->state(),
                 kMaxWaitMs);
}

// Test that a VideoSource can be stopped and restarted.
TEST_F(VideoSourceTest, StopRestart) {
  // Initialize without constraints.
  CreateVideoSource();
  EXPECT_EQ_WAIT(MediaSourceInterface::kLive, state_observer_->state(),
                 kMaxWaitMs);

  ASSERT_TRUE(capturer_->CaptureFrame());
  EXPECT_EQ(1, renderer_.num_rendered_frames());

  source_->Stop();
  EXPECT_EQ_WAIT(MediaSourceInterface::kEnded, state_observer_->state(),
                 kMaxWaitMs);

  source_->Restart();
  EXPECT_EQ_WAIT(MediaSourceInterface::kLive, state_observer_->state(),
                 kMaxWaitMs);

  ASSERT_TRUE(capturer_->CaptureFrame());
  EXPECT_EQ(2, renderer_.num_rendered_frames());

  source_->Stop();
}

// Test start stop with a remote VideoSource - the video source that has a
// RemoteVideoCapturer and takes video frames from FrameInput.
TEST_F(VideoSourceTest, StartStopRemote) {
  source_ = VideoSource::Create(channel_manager_.get(),
                                new webrtc::RemoteVideoCapturer(), NULL, true);

  ASSERT_TRUE(source_.get() != NULL);
  EXPECT_TRUE(NULL != source_->GetVideoCapturer());

  state_observer_.reset(new StateObserver(source_));
  source_->RegisterObserver(state_observer_.get());
  source_->AddSink(&renderer_);

  EXPECT_EQ_WAIT(MediaSourceInterface::kLive, state_observer_->state(),
                 kMaxWaitMs);

  source_->GetVideoCapturer()->Stop();
  EXPECT_EQ_WAIT(MediaSourceInterface::kEnded, state_observer_->state(),
                 kMaxWaitMs);
}

// Test that a VideoSource transition to kEnded if the capture device
// fails.
TEST_F(VideoSourceTest, CameraFailed) {
  CreateVideoSource();
  EXPECT_EQ_WAIT(MediaSourceInterface::kLive, state_observer_->state(),
                 kMaxWaitMs);

  capturer_->SignalStateChange(capturer_, cricket::CS_FAILED);
  EXPECT_EQ_WAIT(MediaSourceInterface::kEnded, state_observer_->state(),
                 kMaxWaitMs);
}

// Test that the capture output is CIF if we set max constraints to CIF.
// and the capture device support CIF.
TEST_F(VideoSourceTest, MandatoryConstraintCif5Fps) {
  FakeConstraints constraints;
  constraints.AddMandatory(MediaConstraintsInterface::kMaxWidth, 352);
  constraints.AddMandatory(MediaConstraintsInterface::kMaxHeight, 288);
  constraints.AddMandatory(MediaConstraintsInterface::kMaxFrameRate, 5);

  CreateVideoSource(&constraints);
  EXPECT_EQ_WAIT(MediaSourceInterface::kLive, state_observer_->state(),
                 kMaxWaitMs);
  const cricket::VideoFormat* format = capturer_->GetCaptureFormat();
  ASSERT_TRUE(format != NULL);
  EXPECT_EQ(352, format->width);
  EXPECT_EQ(288, format->height);
  EXPECT_EQ(30, format->framerate());
}

// Test that the capture output is 720P if the camera support it and the
// optional constraint is set to 720P.
TEST_F(VideoSourceTest, MandatoryMinVgaOptional720P) {
  FakeConstraints constraints;
  constraints.AddMandatory(MediaConstraintsInterface::kMinWidth, 640);
  constraints.AddMandatory(MediaConstraintsInterface::kMinHeight, 480);
  constraints.AddOptional(MediaConstraintsInterface::kMinWidth, 1280);
  constraints.AddOptional(MediaConstraintsInterface::kMinAspectRatio,
                          1280.0 / 720);

  CreateVideoSource(&constraints);
  EXPECT_EQ_WAIT(MediaSourceInterface::kLive, state_observer_->state(),
                 kMaxWaitMs);
  const cricket::VideoFormat* format = capturer_->GetCaptureFormat();
  ASSERT_TRUE(format != NULL);
  EXPECT_EQ(1280, format->width);
  EXPECT_EQ(720, format->height);
  EXPECT_EQ(30, format->framerate());
}

// Test that the capture output have aspect ratio 4:3 if a mandatory constraint
// require it even if an optional constraint request a higher resolution
// that don't have this aspect ratio.
TEST_F(VideoSourceTest, MandatoryAspectRatio4To3) {
  FakeConstraints constraints;
  constraints.AddMandatory(MediaConstraintsInterface::kMinWidth, 640);
  constraints.AddMandatory(MediaConstraintsInterface::kMinHeight, 480);
  constraints.AddMandatory(MediaConstraintsInterface::kMaxAspectRatio,
                           640.0 / 480);
  constraints.AddOptional(MediaConstraintsInterface::kMinWidth, 1280);

  CreateVideoSource(&constraints);
  EXPECT_EQ_WAIT(MediaSourceInterface::kLive, state_observer_->state(),
                 kMaxWaitMs);
  const cricket::VideoFormat* format = capturer_->GetCaptureFormat();
  ASSERT_TRUE(format != NULL);
  EXPECT_EQ(640, format->width);
  EXPECT_EQ(480, format->height);
  EXPECT_EQ(30, format->framerate());
}


// Test that the source state transition to kEnded if the mandatory aspect ratio
// is set higher than supported.
TEST_F(VideoSourceTest, MandatoryAspectRatioTooHigh) {
  FakeConstraints constraints;
  constraints.AddMandatory(MediaConstraintsInterface::kMinAspectRatio, 2);
  CreateVideoSource(&constraints);
  EXPECT_EQ_WAIT(MediaSourceInterface::kEnded, state_observer_->state(),
                 kMaxWaitMs);
}

// Test that the source ignores an optional aspect ratio that is higher than
// supported.
TEST_F(VideoSourceTest, OptionalAspectRatioTooHigh) {
  FakeConstraints constraints;
  constraints.AddOptional(MediaConstraintsInterface::kMinAspectRatio, 2);
  CreateVideoSource(&constraints);
  EXPECT_EQ_WAIT(MediaSourceInterface::kLive, state_observer_->state(),
                 kMaxWaitMs);
  const cricket::VideoFormat* format = capturer_->GetCaptureFormat();
  ASSERT_TRUE(format != NULL);
  double aspect_ratio = static_cast<double>(format->width) / format->height;
  EXPECT_LT(aspect_ratio, 2);
}

// Test that the source starts video with the default resolution if the
// camera doesn't support capability enumeration and there are no constraints.
TEST_F(VideoSourceTest, NoCameraCapability) {
  capturer_->TestWithoutCameraFormats();

  CreateVideoSource();
  EXPECT_EQ_WAIT(MediaSourceInterface::kLive, state_observer_->state(),
                 kMaxWaitMs);
  const cricket::VideoFormat* format = capturer_->GetCaptureFormat();
  ASSERT_TRUE(format != NULL);
  EXPECT_EQ(640, format->width);
  EXPECT_EQ(480, format->height);
  EXPECT_EQ(30, format->framerate());
}

// Test that the source can start the video and get the requested aspect ratio
// if the camera doesn't support capability enumeration and the aspect ratio is
// set.
TEST_F(VideoSourceTest, NoCameraCapability16To9Ratio) {
  capturer_->TestWithoutCameraFormats();

  FakeConstraints constraints;
  double requested_aspect_ratio = 640.0 / 360;
  constraints.AddMandatory(MediaConstraintsInterface::kMinWidth, 640);
  constraints.AddMandatory(MediaConstraintsInterface::kMinAspectRatio,
                           requested_aspect_ratio);

  CreateVideoSource(&constraints);
  EXPECT_EQ_WAIT(MediaSourceInterface::kLive, state_observer_->state(),
                 kMaxWaitMs);
  const cricket::VideoFormat* format = capturer_->GetCaptureFormat();
  double aspect_ratio = static_cast<double>(format->width) / format->height;
  EXPECT_LE(requested_aspect_ratio, aspect_ratio);
}

// Test that the source state transitions to kEnded if an unknown mandatory
// constraint is found.
TEST_F(VideoSourceTest, InvalidMandatoryConstraint) {
  FakeConstraints constraints;
  constraints.AddMandatory("weird key", 640);

  CreateVideoSource(&constraints);
  EXPECT_EQ_WAIT(MediaSourceInterface::kEnded, state_observer_->state(),
                 kMaxWaitMs);
}

// Test that the source ignores an unknown optional constraint.
TEST_F(VideoSourceTest, InvalidOptionalConstraint) {
  FakeConstraints constraints;
  constraints.AddOptional("weird key", 640);

  CreateVideoSource(&constraints);
  EXPECT_EQ_WAIT(MediaSourceInterface::kLive, state_observer_->state(),
                 kMaxWaitMs);
}

TEST_F(VideoSourceTest, SetValidOptionValues) {
  FakeConstraints constraints;
  constraints.AddMandatory(MediaConstraintsInterface::kNoiseReduction, "false");

  CreateVideoSource(&constraints);

  EXPECT_EQ(rtc::Optional<bool>(false),
            source_->options()->video_noise_reduction);
}

TEST_F(VideoSourceTest, OptionNotSet) {
  FakeConstraints constraints;
  CreateVideoSource(&constraints);
  EXPECT_EQ(rtc::Optional<bool>(), source_->options()->video_noise_reduction);
}

TEST_F(VideoSourceTest, MandatoryOptionOverridesOptional) {
  FakeConstraints constraints;
  constraints.AddMandatory(
      MediaConstraintsInterface::kNoiseReduction, true);
  constraints.AddOptional(
      MediaConstraintsInterface::kNoiseReduction, false);

  CreateVideoSource(&constraints);

  EXPECT_EQ(rtc::Optional<bool>(true),
            source_->options()->video_noise_reduction);
}

TEST_F(VideoSourceTest, InvalidOptionKeyOptional) {
  FakeConstraints constraints;
  constraints.AddOptional(
      MediaConstraintsInterface::kNoiseReduction, false);
  constraints.AddOptional("invalidKey", false);

  CreateVideoSource(&constraints);

  EXPECT_EQ_WAIT(MediaSourceInterface::kLive, state_observer_->state(),
      kMaxWaitMs);
  EXPECT_EQ(rtc::Optional<bool>(false),
            source_->options()->video_noise_reduction);
}

TEST_F(VideoSourceTest, InvalidOptionKeyMandatory) {
  FakeConstraints constraints;
  constraints.AddMandatory(
      MediaConstraintsInterface::kNoiseReduction, false);
  constraints.AddMandatory("invalidKey", false);

  CreateVideoSource(&constraints);

  EXPECT_EQ_WAIT(MediaSourceInterface::kEnded, state_observer_->state(),
      kMaxWaitMs);
  EXPECT_EQ(rtc::Optional<bool>(), source_->options()->video_noise_reduction);
}

TEST_F(VideoSourceTest, InvalidOptionValueOptional) {
  FakeConstraints constraints;
  constraints.AddOptional(
      MediaConstraintsInterface::kNoiseReduction, "not a boolean");

  CreateVideoSource(&constraints);

  EXPECT_EQ_WAIT(MediaSourceInterface::kLive, state_observer_->state(),
      kMaxWaitMs);
  EXPECT_EQ(rtc::Optional<bool>(), source_->options()->video_noise_reduction);
}

TEST_F(VideoSourceTest, InvalidOptionValueMandatory) {
  FakeConstraints constraints;
  // Optional constraints should be ignored if the mandatory constraints fail.
  constraints.AddOptional(
      MediaConstraintsInterface::kNoiseReduction, "false");
  // Values are case-sensitive and must be all lower-case.
  constraints.AddMandatory(
      MediaConstraintsInterface::kNoiseReduction, "True");

  CreateVideoSource(&constraints);

  EXPECT_EQ_WAIT(MediaSourceInterface::kEnded, state_observer_->state(),
      kMaxWaitMs);
  EXPECT_EQ(rtc::Optional<bool>(), source_->options()->video_noise_reduction);
}

TEST_F(VideoSourceTest, MixedOptionsAndConstraints) {
  FakeConstraints constraints;
  constraints.AddMandatory(MediaConstraintsInterface::kMaxWidth, 352);
  constraints.AddMandatory(MediaConstraintsInterface::kMaxHeight, 288);
  constraints.AddOptional(MediaConstraintsInterface::kMaxFrameRate, 5);

  constraints.AddMandatory(
      MediaConstraintsInterface::kNoiseReduction, false);
  constraints.AddOptional(
      MediaConstraintsInterface::kNoiseReduction, true);

  CreateVideoSource(&constraints);
  EXPECT_EQ_WAIT(MediaSourceInterface::kLive, state_observer_->state(),
                 kMaxWaitMs);
  const cricket::VideoFormat* format = capturer_->GetCaptureFormat();
  ASSERT_TRUE(format != NULL);
  EXPECT_EQ(352, format->width);
  EXPECT_EQ(288, format->height);
  EXPECT_EQ(30, format->framerate());

  EXPECT_EQ(rtc::Optional<bool>(false),
            source_->options()->video_noise_reduction);
}

// Tests that the source starts video with the default resolution for
// screencast if no constraint is set.
TEST_F(VideoSourceTest, ScreencastResolutionNoConstraint) {
  capturer_->TestWithoutCameraFormats();
  capturer_->SetScreencast(true);

  CreateVideoSource();
  EXPECT_EQ_WAIT(MediaSourceInterface::kLive, state_observer_->state(),
                 kMaxWaitMs);
  const cricket::VideoFormat* format = capturer_->GetCaptureFormat();
  ASSERT_TRUE(format != NULL);
  EXPECT_EQ(640, format->width);
  EXPECT_EQ(480, format->height);
  EXPECT_EQ(30, format->framerate());
}

// Tests that the source starts video with the max width and height set by
// constraints for screencast.
TEST_F(VideoSourceTest, ScreencastResolutionWithConstraint) {
  FakeConstraints constraints;
  constraints.AddMandatory(MediaConstraintsInterface::kMaxWidth, 480);
  constraints.AddMandatory(MediaConstraintsInterface::kMaxHeight, 270);

  capturer_->TestWithoutCameraFormats();
  capturer_->SetScreencast(true);

  CreateVideoSource(&constraints);
  EXPECT_EQ_WAIT(MediaSourceInterface::kLive, state_observer_->state(),
                 kMaxWaitMs);
  const cricket::VideoFormat* format = capturer_->GetCaptureFormat();
  ASSERT_TRUE(format != NULL);
  EXPECT_EQ(480, format->width);
  EXPECT_EQ(270, format->height);
  EXPECT_EQ(30, format->framerate());
}

TEST_F(VideoSourceTest, MandatorySubOneFpsConstraints) {
  FakeConstraints constraints;
  constraints.AddMandatory(MediaConstraintsInterface::kMaxFrameRate, 0.5);

  CreateVideoSource(&constraints);
  EXPECT_EQ_WAIT(MediaSourceInterface::kEnded, state_observer_->state(),
                 kMaxWaitMs);
  ASSERT_TRUE(capturer_->GetCaptureFormat() == NULL);
}

TEST_F(VideoSourceTest, OptionalSubOneFpsConstraints) {
  FakeConstraints constraints;
  constraints.AddOptional(MediaConstraintsInterface::kMaxFrameRate, 0.5);

  CreateVideoSource(&constraints);
  EXPECT_EQ_WAIT(MediaSourceInterface::kLive, state_observer_->state(),
                 kMaxWaitMs);
  const cricket::VideoFormat* format = capturer_->GetCaptureFormat();
  ASSERT_TRUE(format != NULL);
  EXPECT_EQ(30, format->framerate());
}
