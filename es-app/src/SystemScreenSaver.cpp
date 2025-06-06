#include "SystemScreenSaver.h"

#ifdef _RPI_
#include "components/VideoPlayerComponent.h"
#endif
#include "components/VideoVlcComponent.h"
#include "utils/FileSystemUtil.h"
#include "views/gamelist/IGameListView.h"
#include "views/ViewController.h"
#include "FileData.h"
#include "FileFilterIndex.h"
#include "Log.h"
#include "PowerSaver.h"
#include "Sound.h"
#include "SystemData.h"
#include "components/ImageComponent.h"
#include "components/TextComponent.h"
#include <unordered_map>
#include <time.h>
#include "AudioManager.h"
#include "math/Vector2i.h"
#include "SystemConf.h"

#define FADE_TIME					(500)
#define DATE_TIME_UPDATE_INTERVAL	(100)

SystemScreenSaver::SystemScreenSaver(Window* window) :
	mVideoScreensaver(NULL),
	mImageScreensaver(NULL),
	mWindow(window),
	mVideosCounted(false),
	mVideoCount(0),
	mImagesCounted(false),
	mImageCount(0),
	mState(STATE_INACTIVE),
	mOpacity(0.0f),
	mTimer(0),
	mSystemName(""),
	mGameName(""),
	mCurrentGame(NULL),
	mLoadingNext(false)
{

	mWindow->setScreenSaver(this);
	std::string path = getTitleFolder();
	if(!Utils::FileSystem::exists(path))
		Utils::FileSystem::createDirectory(path);
	srand((unsigned int)time(NULL));
	mVideoChangeTime = 30000;

	mDefaultRandomEngine = new default_random_engine(mRandomDevice());

}

SystemScreenSaver::~SystemScreenSaver()
{
	// Delete subtitle file, if existing
	remove(getTitlePath().c_str());
	mCurrentGame = NULL;

	if( mDefaultRandomEngine != nullptr)
	{
		delete mDefaultRandomEngine;
		mDefaultRandomEngine = nullptr;
	}
	
}

bool SystemScreenSaver::allowSleep()
{
	return (mVideoScreensaver == nullptr && mImageScreensaver == nullptr);
}

bool SystemScreenSaver::isScreenSaverActive()
{
	return (mState != STATE_INACTIVE);
}

void SystemScreenSaver::startScreenSaver()
{
	bool loadingNext = mLoadingNext;

	stopScreenSaver();

	std::string screensaver_behavior = Settings::getInstance()->getString("ScreenSaverBehavior");
	if (screensaver_behavior == "random video")
	{
		if (!loadingNext && Settings::getInstance()->getBool("VideoAudio"))
			//AudioManager::getInstance()->deinit();

		mVideoChangeTime = Settings::getInstance()->getInt("ScreenSaverSwapVideoTimeout");

		// Configure to fade out the windows, Skip Fading if Instant mode
		mState =  PowerSaver::getMode() == PowerSaver::INSTANT
					? STATE_SCREENSAVER_ACTIVE
					: STATE_FADE_OUT_WINDOW;
	
		if (mState == STATE_FADE_OUT_WINDOW)
		{
			mState = STATE_FADE_IN_VIDEO;
			mOpacity = 1.0f;
		}
		else
			mOpacity = 0.0f;
			
		// Load a random video
		std::string path = pickRandomVideo();

		int retry = 200;
		while (retry > 0 && !Utils::FileSystem::exists(path))
		{
			retry--;
			path = pickRandomVideo();
		}

		if (!path.empty() && Utils::FileSystem::exists(path))
		{
			LOG(LogDebug) << "VideoScreenSaver::startScreenSaver " << path.c_str();

			mVideoScreensaver = std::make_shared<VideoScreenSaver>(mWindow);
			mVideoScreensaver->setGame(mCurrentGame);
			mVideoScreensaver->setVideo(path);

			PowerSaver::runningScreenSaver(true);
			mTimer = 0;
			return;
		}
	}
	else if (screensaver_behavior == "slideshow")
	{
		mVideoChangeTime = Settings::getInstance()->getInt("ScreenSaverSwapImageTimeout");

		// Configure to fade out the windows, Skip Fading if Instant mode
		mState = PowerSaver::getMode() == PowerSaver::INSTANT
			? STATE_SCREENSAVER_ACTIVE
			: STATE_FADE_OUT_WINDOW;

		if (mState == STATE_FADE_OUT_WINDOW)
		{
			mState = STATE_FADE_IN_VIDEO;
			mOpacity = 1.0f;
		}
		else
			mOpacity = 0.0f;

		// Load a random image
		std::string path;
		if (Settings::getInstance()->getBool("SlideshowScreenSaverCustomImageSource"))
		{
			path = pickRandomCustomImage();
			// Custom images are not tied to the game list
			mCurrentGame = NULL;
		}
		else
			path = pickRandomGameListImage();

		if (!path.empty() && Utils::FileSystem::exists(path))
		{
			LOG(LogDebug) << "ImageScreenSaver::startScreenSaver " << path.c_str();

			mImageScreensaver = std::make_shared<ImageScreenSaver>(mWindow);
			mImageScreensaver->setGame(mCurrentGame);
			mImageScreensaver->setImage(path);

			PowerSaver::runningScreenSaver(true);
			mTimer = 0;
			return;
		}	
	}

	// No videos. Just use a standard screensaver
	mState = STATE_SCREENSAVER_ACTIVE;
	mCurrentGame = NULL;
}

void SystemScreenSaver::stopScreenSaver()
{
	bool isExitingScreenSaver = !mLoadingNext;

	if (mLoadingNext)
		mFadingImageScreensaver = mImageScreensaver;
	else
		mFadingImageScreensaver = nullptr;

	// so that we stop the background audio next time, unless we're restarting the screensaver
	mLoadingNext = false;

	mVideoScreensaver = nullptr;
	mImageScreensaver = nullptr;

	// we need this to loop through different videos
	mState = STATE_INACTIVE;
	PowerSaver::runningScreenSaver(false);

	// Exiting video screen saver -> Restore sound
	if (isExitingScreenSaver && Settings::getInstance()->getBool("VideoAudio") && mVideoScreensaver)
	{
		AudioManager::getInstance()->init();
		AudioManager::getInstance()->playRandomMusic();
	}
}

void SystemScreenSaver::renderScreenSaver()
{
	Transform4x4f transform = Transform4x4f::Identity();
	
	if (mVideoScreensaver)
	{
		// Render black background
		Renderer::setMatrix(Transform4x4f::Identity());
		Renderer::drawRect(0.0f, 0.0f, Renderer::getScreenWidth(), Renderer::getScreenHeight(), 0x000000FF, 0x000000FF);

		// Only render the video if the state requires it
		if ((int)mState >= STATE_FADE_IN_VIDEO)
		{
			unsigned int opacity = 255 - (unsigned char)(mOpacity * 255);

			mVideoScreensaver->setOpacity(opacity);
			mVideoScreensaver->render(transform);
		}
	}
	else if (mImageScreensaver)
	{
		// Render black background
		Renderer::setMatrix(transform);
		Renderer::drawRect(0.0f, 0.0f, Renderer::getScreenWidth(), Renderer::getScreenHeight(), 0x000000FF);

		if (mFadingImageScreensaver != nullptr)		
			mFadingImageScreensaver->render(transform);

		// Only render the video if the state requires it
		if ((int)mState >= STATE_FADE_IN_VIDEO)
		{			
			if (mImageScreensaver->hasImage())
			{
				unsigned int opacity = 255 - (unsigned char)(mOpacity * 255);
													
				Renderer::setMatrix(transform);
				Renderer::drawRect(0.0f, 0.0f, Renderer::getScreenWidth(), Renderer::getScreenHeight(), 0x00000000 | opacity);
				mImageScreensaver->setOpacity(opacity);
				mImageScreensaver->render(transform);
			}
		}
	}
	else if (mState != STATE_INACTIVE)
	{
		std::string screensaver_behavior = Settings::getInstance()->getString("ScreenSaverBehavior");

		Renderer::setMatrix(Transform4x4f::Identity());
		unsigned char color = screensaver_behavior == "dim" ? 0x000000A0 : 0x000000FF;
		Renderer::drawRect(0.0f, 0.0f, Renderer::getScreenWidth(), Renderer::getScreenHeight(), color, color);
	}
}

unsigned long SystemScreenSaver::countGameListNodes(const char *nodeName)
{
	unsigned long nodeCount = 0;
	std::vector<SystemData*>::const_iterator it;
	for (it = SystemData::sSystemVector.cbegin(); it != SystemData::sSystemVector.cend(); ++it)
	{
		// We only want nodes from game systems that are not collections
		if (!(*it)->isGameSystem() || (*it)->isCollection())
			continue;

		FolderData* rootFileData = (*it)->getRootFolder();

		std::vector<FileData*> allFiles = rootFileData->getFilesRecursive(GAME, true);
		std::vector<FileData*>::const_iterator itf;  // declare an iterator to a vector of strings

		for (itf=allFiles.cbegin() ; itf < allFiles.cend(); itf++) 
		{
			if ((strcmp(nodeName, "video") == 0 && (*itf)->getVideoPath() != "") ||
				(strcmp(nodeName, "image") == 0 && (*itf)->getImagePath() != ""))
			{
				nodeCount++;
			}
		}
	}
	return nodeCount;
}

void SystemScreenSaver::countVideos()
{
	if (!mVideosCounted)
	{
		mVideoCount = countGameListNodes("video");
		mVideosCounted = true;
	}
}

void SystemScreenSaver::countImages()
{
	if (!mImagesCounted)
	{
		mImageCount = countGameListNodes("image");
		mImagesCounted = true;
	}
}

std::string SystemScreenSaver::pickGameListNode(unsigned long index, const char *nodeName)
{
	std::string path;

	std::vector<SystemData*>::const_iterator it;
	for (it = SystemData::sSystemVector.cbegin(); it != SystemData::sSystemVector.cend(); ++it)
	{
		// We only want nodes from game systems that are not collections
		if (!(*it)->isGameSystem() || (*it)->isCollection())
			continue;

		FolderData* rootFileData = (*it)->getRootFolder();

		FileType type = GAME;
		std::vector<FileData*> allFiles = rootFileData->getFilesRecursive(type, true);
		std::vector<FileData*>::const_iterator itf;  // declare an iterator to a vector of strings

		for(itf=allFiles.cbegin() ; itf < allFiles.cend(); itf++) 
		{
			if ((strcmp(nodeName, "video") == 0 && (*itf)->getVideoPath() != "") ||
				(strcmp(nodeName, "image") == 0 && (*itf)->getImagePath() != ""))
			{
				if (index-- <= 0)
				{
					// We have it
					if (strcmp(nodeName, "video") == 0)
						path = (*itf)->getVideoPath();
					else if (strcmp(nodeName, "image") == 0)
						path = (*itf)->getImagePath();

					if (!Utils::FileSystem::exists(path))
						continue;

					mSystemName = (*it)->getFullName();
					mGameName = (*itf)->getName();
					mCurrentGame = (*itf);

#ifdef _RPI_
					if (Settings::getInstance()->getBool("ScreenSaverOmxPlayer"))
						if (Settings::getInstance()->getString("ScreenSaverGameInfo") != "never" && strcmp(nodeName, "video") == 0)
							writeSubtitle(mGameName.c_str(), mSystemName.c_str(), (Settings::getInstance()->getString("ScreenSaverGameInfo") == "always"));
#endif

					return path;
				}
			}
		}
	}

	return "";
}

std::string SystemScreenSaver::pickRandomVideo()
{
	countVideos();
	mCurrentGame = NULL;
	if (mVideoCount == 0)
		return "";
	
	//int video = (int)(((float)rand() / float(RAND_MAX)) * (float)mVideoCount);

	uniform_int_distribution<int> uniform_dist(0, mVideoCount -1);
	int video = uniform_dist(*mDefaultRandomEngine);

	return pickGameListNode(video, "video");
}

std::string SystemScreenSaver::pickRandomGameListImage()
{
	countImages();
	mCurrentGame = NULL;
	if (mImageCount == 0)
		return "";
	
	//int image = (int)(((float)rand() / float(RAND_MAX)) * (float)mImageCount);
	uniform_int_distribution<int> uniform_dist(0, mImageCount -1);
	int image = uniform_dist(*mDefaultRandomEngine);
	
	return pickGameListNode(image, "image");
}

std::string SystemScreenSaver::pickRandomCustomImage()
{
	std::string path;

	std::string imageDir = Settings::getInstance()->getString("SlideshowScreenSaverImageDir");
	if ((imageDir != "") && (Utils::FileSystem::exists(imageDir)))
	{
		std::string                   imageFilter = Settings::getInstance()->getString("SlideshowScreenSaverImageFilter");
		std::vector<std::string>      matchingFiles;
		Utils::FileSystem::stringList dirContent  = Utils::FileSystem::getDirContent(imageDir, Settings::getInstance()->getBool("SlideshowScreenSaverRecurse"));

		for(Utils::FileSystem::stringList::const_iterator it = dirContent.cbegin(); it != dirContent.cend(); ++it)
		{
			if (Utils::FileSystem::isRegularFile(*it))
			{
				// If the image filter is empty, or the file extension is in the filter string,
				//  add it to the matching files list
				if ((imageFilter.length() <= 0) ||
					(imageFilter.find(Utils::FileSystem::getExtension(*it)) != std::string::npos))
				{
					matchingFiles.push_back(*it);
				}
			}
		}

		int fileCount = (int)matchingFiles.size();
		if (fileCount > 0)
		{
			// get a random index in the range 0 to fileCount (exclusive)
			int randomIndex = rand() % fileCount;
			path = matchingFiles[randomIndex];
		}
		else
		{
			LOG(LogError) << "Slideshow Screensaver - No image files found\n";
		}
	}
	else
	{
		LOG(LogError) << "Slideshow Screensaver - Image directory does not exist: " << imageDir << "\n";
	}

	return path;
}

void SystemScreenSaver::update(int deltaTime)
{
	// Use this to update the fade value for the current fade stage
	if (mState == STATE_FADE_OUT_WINDOW)
	{
		mOpacity += (float)deltaTime / FADE_TIME;
		if (mOpacity >= 1.0f)
		{
			mOpacity = 1.0f;

			// Update to the next state
			mState = STATE_FADE_IN_VIDEO;			
		}
	}
	else if (mState == STATE_FADE_IN_VIDEO)
	{
		mOpacity -= (float)deltaTime / FADE_TIME;
		if (mOpacity <= 0.0f)
		{
			mOpacity = 0.0f;
			// Update to the next state
			mState = STATE_SCREENSAVER_ACTIVE;
			mFadingImageScreensaver = nullptr;
		}
	}
	else if (mState == STATE_SCREENSAVER_ACTIVE)
	{
		// Update the timer that swaps the videos
		mTimer += deltaTime;
		if (mTimer > mVideoChangeTime)
			nextVideo();
	}

	// If we have a loaded video then update it
	if (mVideoScreensaver)
		mVideoScreensaver->update(deltaTime);

	if (mImageScreensaver)
		mImageScreensaver->update(deltaTime);
}

void SystemScreenSaver::nextVideo() 
{
	mLoadingNext = true;
	startScreenSaver();
}

FileData* SystemScreenSaver::getCurrentGame()
{
	return mCurrentGame;
}

void SystemScreenSaver::launchGame()
{
	if (mCurrentGame != NULL)
	{
		// launching Game
		auto view = ViewController::get()->getGameListView(mCurrentGame->getSystem(), false);
		if (view != nullptr)
			view->setCursor(mCurrentGame);

		if (Settings::getInstance()->getBool("ScreenSaverControls"))
			mCurrentGame->launchGame(mWindow);
		else
			ViewController::get()->goToGameList(mCurrentGame->getSystem());
	}
}


// ------------------------------------------------------------------------------------------------------------------------
// GAME SCREEN SAVER BASE CLASS
// ------------------------------------------------------------------------------------------------------------------------

GameScreenSaverBase::GameScreenSaverBase(Window* window) : GuiComponent(window),
	mViewport(0, 0, Renderer::getScreenWidth(), Renderer::getScreenHeight())
{
	mDecoration = nullptr;
	mMarquee = nullptr;
	mLabelGame = nullptr;
	mLabelSystem = nullptr;
	mLabelDate = nullptr;
	mLabelTime = nullptr;
	mDateTimeUpdateAccumulator = 0;
	mDateTimeLastUpdate = 0;

	if (Settings::getInstance()->getBool("ScreenSaverDateTime"))
	{
		auto ph = ThemeData::getMenuTheme()->Text.font->getPath();
		auto sz = mViewport.h / 16.f;
		auto margin = sz / 2.f;
		auto font = Font::get(sz, ph);
		int fh = font->getLetterHeight();

		mLabelDate = new TextComponent(mWindow);
		mLabelDate->setPosition(mViewport.x + margin, mViewport.y + margin);
		mLabelDate->setSize(mViewport.w, sz * 0.66);
		mLabelDate->setHorizontalAlignment(ALIGN_LEFT);
		mLabelDate->setVerticalAlignment(ALIGN_CENTER);
		mLabelDate->setColor(0xD0D0D0FF);
		mLabelDate->setGlowColor(0x00000060);
		mLabelDate->setGlowSize(2);
		mLabelDate->setFont(ph, sz * 0.66);

		mLabelTime = new TextComponent(mWindow);
		mLabelTime->setPosition(mViewport.x + margin, mViewport.y + margin + mLabelDate->getSize().y() * 1.3f);
		mLabelTime->setSize(mViewport.w, fh);
		mLabelTime->setHorizontalAlignment(ALIGN_LEFT);
		mLabelTime->setVerticalAlignment(ALIGN_CENTER);
		mLabelTime->setColor(0xFFFFFFFF);
		mLabelTime->setGlowColor(0x00000040);
		mLabelTime->setGlowSize(3);
		mLabelTime->setFont(font);
	}
}

GameScreenSaverBase::~GameScreenSaverBase()
{
	if (mMarquee != nullptr)
	{
		delete mMarquee;
		mMarquee = nullptr;
	}

	if (mDecoration != nullptr)
	{
		delete mDecoration;
		mDecoration = nullptr;
	}

	if (mLabelGame != nullptr)
	{
		delete mLabelGame;
		mLabelGame = nullptr;
	}

	if (mLabelSystem != nullptr)
	{
		delete mLabelSystem;
		mLabelSystem = nullptr;
	}

	if (mLabelDate != nullptr)
	{
		delete mLabelDate;
		mLabelDate = nullptr;
	}

	if (mLabelTime != nullptr)
	{
		delete mLabelTime;
		mLabelTime = nullptr;
	}
}

#include "guis/GuiMenu.h"
#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
#include <rapidjson/filereadstream.h>

void GameScreenSaverBase::setGame(FileData* game)
{	
	if (mLabelGame != nullptr)
	{
		delete mLabelGame;
		mLabelGame = nullptr;
	}

	if (mLabelSystem != nullptr)
	{
		delete mLabelSystem;
		mLabelSystem = nullptr;
	}

	if (mMarquee != nullptr)
	{
		delete mMarquee;
		mMarquee = nullptr;
	}

	if (mDecoration != nullptr)
	{
		delete mDecoration;
		mDecoration = nullptr;
	}

	if (game == nullptr)
		return;

	mViewport = Renderer::Rect(0, 0, Renderer::getScreenWidth(), Renderer::getScreenHeight());

	/*
#ifdef _RPI_
	if (!Settings::getInstance()->getBool("ScreenSaverOmxPlayer"))
#endif
	if (Settings::getInstance()->getBool("ScreenSaverDecoration"))
	{
		auto sets = GuiMenu::getDecorationsSets(game->getSystem());
		int setId = (int)(((float)rand() / float(RAND_MAX)) * (float)sets.size());

		if (setId >= 0 && setId < sets.size() && Utils::FileSystem::exists(sets[setId].imageUrl))
		{
			std::string infoFile = Utils::String::replace(sets[setId].imageUrl, ".png", ".info");
			if (Utils::FileSystem::exists(infoFile))
			{
				FILE* fp = fopen(infoFile.c_str(), "r"); // non-Windows use "r"
				if (fp)
				{
					char readBuffer[65536];
					rapidjson::FileReadStream is(fp, readBuffer, sizeof(readBuffer));
					rapidjson::Document doc;
					doc.ParseStream(is);

					if (!doc.HasParseError())
					{
						if (doc.HasMember("top") && doc.HasMember("left") && doc.HasMember("bottom") && doc.HasMember("right") && doc.HasMember("width") && doc.HasMember("height"))
						{
							auto width = doc["width"].GetInt();
							auto height = doc["height"].GetInt();
							if (width > 0 && height > 0)
							{
								float px = Renderer::getScreenWidth() / (float)width;
								float py = Renderer::getScreenHeight() / (float)height;

								auto top = doc["top"].GetInt();
								auto left = doc["left"].GetInt();
								auto bottom = doc["bottom"].GetInt();
								auto right = doc["right"].GetInt();

								mViewport = Renderer::Rect(left * px, top * py, (width - right) * px, (height - bottom) * py);
							}
						}
					}

					fclose(fp);
				}
			}

			mDecoration = new ImageComponent(mWindow, true);
			mDecoration->setImage(sets[setId].imageUrl);
			mDecoration->setOrigin(0.5f, 0.5f);
			mDecoration->setPosition(Renderer::getScreenWidth() / 2.0f, (float)Renderer::getScreenHeight() / 2.0f);
			mDecoration->setMaxSize((float)Renderer::getScreenWidth(), (float)Renderer::getScreenHeight());
		}
	}
	*/
	if (!Settings::getInstance()->getBool("SlideshowScreenSaverGameName"))
		return;

	if (Settings::getInstance()->getBool("ScreenSaverMarquee") && Utils::FileSystem::exists(game->getMarqueePath()))
	{
		mMarquee = new ImageComponent(mWindow, true);
		mMarquee->setImage(game->getMarqueePath());
		mMarquee->setOrigin(0.5f, 0.5f);
		mMarquee->setPosition(mViewport.x / 2.0f + mViewport.w * 0.50f, mViewport.y / 2.0f + mViewport.h * 0.18f);
		mMarquee->setMaxSize((float)mViewport.w * 0.40f, (float)mViewport.h * 0.22f);
	}
	
	auto ph = ThemeData::getMenuTheme()->Text.font->getPath();
	auto sz = mViewport.h / 16.f;
	auto font = Font::get(sz, ph);

	int h = mViewport.h / 4.0f;
	int fh = font->getLetterHeight();

	mLabelGame = new TextComponent(mWindow);
	mLabelGame->setPosition(mViewport.x / 2.0f, mViewport.y / 2.0f + mViewport.h - h - fh / 2);
	mLabelGame->setSize(mViewport.w, h - fh / 2);
	mLabelGame->setHorizontalAlignment(ALIGN_CENTER);
	mLabelGame->setVerticalAlignment(ALIGN_CENTER);
	mLabelGame->setColor(0xFFFFFFFF);
	mLabelGame->setGlowColor(0x00000040);
	mLabelGame->setGlowSize(3);
	mLabelGame->setFont(font);
	mLabelGame->setText(game->getName());

	mLabelSystem = new TextComponent(mWindow);
	mLabelSystem->setPosition(mViewport.x / 2.0f, mViewport.y / 2.0f + mViewport.h - h + fh / 2);
	mLabelSystem->setSize(mViewport.w, h + fh / 2);
	mLabelSystem->setHorizontalAlignment(ALIGN_CENTER);
	mLabelSystem->setVerticalAlignment(ALIGN_CENTER);
	mLabelSystem->setColor(0xD0D0D0FF);
	mLabelSystem->setGlowColor(0x00000060);
	mLabelSystem->setGlowSize(2);
	mLabelSystem->setFont(ph, sz * 0.66);
	mLabelSystem->setText(game->getSystem()->getFullName());
}

void GameScreenSaverBase::render(const Transform4x4f& transform)
{
	if (mMarquee)
	{
		mMarquee->setOpacity(mOpacity);
		mMarquee->render(transform);
	}
	else if (mLabelGame)
	{
		mLabelGame->setOpacity(mOpacity);
		mLabelGame->render(transform);
	}

	if (mLabelSystem)
	{
		mLabelSystem->setOpacity(mOpacity);
		mLabelSystem->render(transform);
	}

	if (mDecoration)
	{
		mDecoration->setOpacity(mOpacity);
		mDecoration->render(transform);
	}

	if (mLabelDate)
	{
		mLabelDate->setOpacity(255);
		mLabelDate->render(transform);
	}

	if (mLabelTime)
	{
		mLabelTime->setOpacity(255);
		mLabelTime->render(transform);
	}
}

void GameScreenSaverBase::update(int deltaTime)
{
	GuiComponent::update(deltaTime);
	
	if (Settings::getInstance()->getBool("ScreenSaverDateTime"))
	{
		mDateTimeUpdateAccumulator += deltaTime;
		if (mDateTimeUpdateAccumulator >= DATE_TIME_UPDATE_INTERVAL)
		{
			mDateTimeUpdateAccumulator -= DATE_TIME_UPDATE_INTERVAL;

			time_t now = time(NULL);
			if (now != mDateTimeLastUpdate)
			{
				mDateTimeLastUpdate = now;

				struct tm* timeinfo = localtime(&now);

				const std::string& dateFormat = Settings::getInstance()->getString("ScreenSaverDateFormat");
				const std::string& timeFormat = Settings::getInstance()->getString("ScreenSaverTimeFormat");

				char dateBuffer[64];
				char timeBuffer[64];
				strftime(dateBuffer, sizeof(dateBuffer), dateFormat.c_str(), timeinfo);
				strftime(timeBuffer, sizeof(timeBuffer), timeFormat.c_str(), timeinfo);

				if (mLabelDate)
					mLabelDate->setText(std::string(dateBuffer));

				if (mLabelTime)
					mLabelTime->setText(std::string(timeBuffer));
			}
		}
	}
}

void GameScreenSaverBase::setOpacity(unsigned char opacity)
{
	mOpacity = opacity;
}


// ------------------------------------------------------------------------------------------------------------------------
// IMAGE SCREEN SAVER CLASS
// ------------------------------------------------------------------------------------------------------------------------

ImageScreenSaver::ImageScreenSaver(Window* window) : GameScreenSaverBase(window)
{
	mImage = nullptr;
}

ImageScreenSaver::~ImageScreenSaver()
{
	if (mImage != nullptr)
		delete mImage;
}

void ImageScreenSaver::setImage(const std::string path)
{
	if (mImage == nullptr)
	{
		mImage = new ImageComponent(mWindow, true);
		mImage->setOrigin(0.5f, 0.5f);
		mImage->setPosition((mViewport.x + mViewport.w) / 2.0f, (mViewport.y + mViewport.h) / 2.0f);

		if (Settings::getInstance()->getBool("SlideshowScreenSaverStretch"))
			mImage->setMinSize((float)mViewport.w, (float)mViewport.h);
		else
			mImage->setMaxSize((float)mViewport.w, (float)mViewport.h);
	}

	mImage->setImage(path);
}

bool ImageScreenSaver::hasImage()
{
	return mImage != nullptr && mImage->hasImage();
}

void ImageScreenSaver::render(const Transform4x4f& transform)
{
	if (mImage)
	{
		mImage->setOpacity(mOpacity);

		Renderer::pushClipRect(Vector2i(mViewport.x, mViewport.y), Vector2i(mViewport.w, mViewport.h));
		mImage->render(transform);
		Renderer::popClipRect();
	}

	GameScreenSaverBase::render(transform);
}


// ------------------------------------------------------------------------------------------------------------------------
// VIDEO SCREEN SAVER CLASS
// ------------------------------------------------------------------------------------------------------------------------

VideoScreenSaver::VideoScreenSaver(Window* window) : GameScreenSaverBase(window)
{
	mVideo = nullptr;
	mTime = 0;
	mFade = 1.0;
}

VideoScreenSaver::~VideoScreenSaver()
{
	if (mVideo != nullptr)
		delete mVideo;
}

void VideoScreenSaver::setVideo(const std::string path)
{
	if (mVideo == nullptr)
	{
#ifdef _RPI_
		// Create the correct type of video component
		if (Settings::getInstance()->getBool("ScreenSaverOmxPlayer"))
			mVideo = new VideoPlayerComponent(mWindow, getTitlePath());
		else
#endif
		mVideo = new VideoVlcComponent(mWindow);

		mVideo->topWindow(true);
		mVideo->setOrigin(0.5f, 0.5f);
		
		mVideo->setPosition((mViewport.x + mViewport.w) / 2.0f, (mViewport.y + mViewport.h) / 2.0f);

		if (Settings::getInstance()->getBool("StretchVideoOnScreenSaver"))
			mVideo->setMinSize((float)mViewport.w, (float)mViewport.h);
		else
			mVideo->setMaxSize((float)mViewport.w, (float)mViewport.h);

		mVideo->setVideo(path);
		mVideo->setScreensaverMode(true);
		mVideo->onShow();
	}

	mFade = 1.0;
	mTime = 0;
	mVideo->setVideo(path);
}

#define SUBTITLE_DURATION 4000
#define SUBTITLE_FADE 150

void VideoScreenSaver::render(const Transform4x4f& transform)
{	
	if (mVideo)
	{		
		mVideo->setOpacity(mOpacity);

		Renderer::pushClipRect(Vector2i(mViewport.x, mViewport.y), Vector2i(mViewport.w, mViewport.h));
		mVideo->render(transform);
		Renderer::popClipRect();
	}

#ifdef _RPI_
	if (Settings::getInstance()->getBool("ScreenSaverOmxPlayer"))
		return;
#endif

	if (Settings::getInstance()->getString("ScreenSaverGameInfo") != "never")
	{
		if (mMarquee && mFade != 0)
		{
			mMarquee->setOpacity(mOpacity * mFade);
			mMarquee->render(transform);
		}
		else if (mLabelGame && mFade != 0)
		{
			mLabelGame->setOpacity(mOpacity * mFade);
			mLabelGame->render(transform);
		}

		if (mLabelSystem && mFade != 0)
		{
			mLabelSystem->setOpacity(mOpacity * mFade);
			mLabelSystem->render(transform);
		}
	}
	
	if (mDecoration)
	{
		mDecoration->setOpacity(mOpacity);
		mDecoration->render(transform);		
	}

	if (mLabelDate)
	{
		mLabelDate->setOpacity(255);
		mLabelDate->render(transform);
	}

	if (mLabelTime)
	{
		mLabelTime->setOpacity(255);
		mLabelTime->render(transform);
	}
}

void VideoScreenSaver::update(int deltaTime)
{
	GameScreenSaverBase::update(deltaTime);

	if (mVideo)
	{
		if (Settings::getInstance()->getString("ScreenSaverGameInfo") == "start & end")
		{
			int duration = SUBTITLE_DURATION;
			int end = Settings::getInstance()->getInt("ScreenSaverSwapVideoTimeout") - duration;

			if (mTime >= duration - SUBTITLE_FADE && mTime < duration)
			{
				mFade -= (float)deltaTime / SUBTITLE_FADE;
				if (mFade < 0)
					mFade = 0;
			}
			else if (mTime >= end - SUBTITLE_FADE && mTime < end)
			{
				mFade += (float)deltaTime / SUBTITLE_FADE;
				if (mFade > 1)
					mFade = 1;
			}
			else if (mTime > duration && mTime < end - SUBTITLE_FADE)
				mFade = 0;
		}
	
		mTime += deltaTime;	
		mVideo->update(deltaTime);
	}
}