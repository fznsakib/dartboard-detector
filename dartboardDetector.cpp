/* Compile on personal Linux:
g++ dartboardDetector.cpp /usr/lib/libopencv_core.so.2.4
/usr/lib/libopencv_highgui.so.2.4 -lopencv_imgproc -lopencv_objdetect */

// Header Inclusion
#include <stdio.h>
#include "opencv2/objdetect/objdetect.hpp"
#include "opencv2/opencv.hpp"
#include "opencv2/core/core.hpp"
#include "opencv2/highgui/highgui.hpp"
#include "opencv2/imgproc/imgproc.hpp"
#include <iostream>
#include <string>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <vector>
#include <stdio.h>
#include <unistd.h>

#define PI 3.14159265

using namespace std;
using namespace cv;

/** Initialise Classes **/
class lineData {
  public:
    double m, c, theta, rho;
    Point point1, point2;
    bool operator< (const lineData &other) const {
        return theta < other.theta;
    }
    lineData(double m, double c, double theta, double rho,
             Point point1, Point point2)
             : m(m), c(c), theta(theta), rho(rho), point1(point1),
             point2(point2) {}
};

/** Function Headers */
void detectVJ( Mat frame );
void displayVJ( Mat frame );
void displayHough( Mat frame );
float F1Test( int facesDetected, const char* imgName, Mat frame, int detectorChoice );
void removeOverlaps();

void convolution(	Mat &input,	int size,	int direction,	Mat kernel,	Mat &output );
void getMagnitude( Mat &dfdx, Mat &dfdy, Mat &output );
void getDirection( Mat &dfdx, Mat &dfdy, Mat &output );
void getThresholdedMag(	Mat &input,	Mat &output );
void getHoughSpace(	Mat &thresholdedMag, Mat &gradientDirection, int threshold, int width,	int height,	Mat &output,
                    std::vector<double> &rhoValues, std::vector<double> &thetaValues);
void extractLines( Mat &frame, Mat &croppedImg, std::vector<double> &rhoValues, std::vector<double> &thetaValues, vector<lineData> &lines );
vector<Vec3f> extractCircles( Mat &frame );
void drawLines( Mat &frame, vector<lineData> &lines );
vector<lineData> filterLines( vector<lineData> &lines, int tolerance );
void dartboardDetector( Mat &frame, Mat &drawingFrame, Mat &croppedImg, Rect box );
Rect reduceBox( Rect box );
void detectionDecision( Mat &frame, Mat &croppedImg, Rect box, vector<lineData> &lines, vector<Vec3f> &circles);

void analyseBoxes( Mat &frame );
std::vector<Rect> mergeDetections();
Mat grabCut( Mat frame, int view );


/** Global variables */
String cascade_name = "cascade.xml";
CascadeClassifier cascade;

std::vector<Rect> detectedDartboardsVJ;
std::vector<Rect> detectedDartboardsFinal;
std::vector<Rect> trueDartboards;
vector<Vec3f> allCircles;

/* Functions */

void convolution(Mat &input, int size, int direction, Mat kernel, Mat &output) {

  int kernelRadiusX = ( kernel.size[0] - 1 ) / 2;
	int kernelRadiusY = ( kernel.size[1] - 1 ) / 2;

  // Create padded version of input
  Mat paddedInput;
	copyMakeBorder( input, paddedInput,
		kernelRadiusX, kernelRadiusX, kernelRadiusY, kernelRadiusY,
		BORDER_REPLICATE );

	// Gaussian blur before finding derivation
	GaussianBlur(paddedInput, paddedInput, Size(3,3), 0, 0, BORDER_DEFAULT);

  // Time to do convolution
  for (int i = 0; i < input.rows; i++) {
     for (int j = 0; j < input.cols; j++) {

       double sum = 0.0;

       for(int m = -kernelRadiusX; m <= kernelRadiusX; m++) {
         for(int n = -kernelRadiusY; n <= kernelRadiusY; n++) {

           // find the correct indices we are using
 					int imagex = i + m + kernelRadiusX;
 					int imagey = j + n + kernelRadiusY;
 					int kernelx = m + kernelRadiusX;
 					int kernely = n + kernelRadiusY;

 					// get the values from the padded image and the kernel
 					int imageval = (int) paddedInput.at<uchar>( imagex, imagey );
 					double kernalval = kernel.at<double>( kernelx, kernely );

 					// do the multiplication
 					sum += imageval * kernalval;
         }
       }
       output.at<double>(i, j) = sum;
    }
  }

	Mat img;
	img.create(input.size(), CV_64F);
	// Normalise to avoid out of range and negative values
	normalize(output, img, 0, 255, NORM_MINMAX);

  //Save thresholded image
	if (direction == 0) imwrite("output/dfdx.jpg", img);
	else imwrite("output/dfdy.jpg", img);
}

void getMagnitude(Mat &dfdx, Mat &dfdy,	Mat &output) {
  for (int y = 0; y < output.rows; y++) {
    for (int x = 0; x < output.cols; x++) {

      double dxVal = 0.0;
      double dyVal = 0.0;
      double magnitudeVal = 0.0;

      dxVal = dfdx.at<double>(y, x);
      dyVal = dfdy.at<double>(y, x);

			// Calculate magnitude
      magnitudeVal = sqrt(pow(dxVal, 2) + pow(dyVal, 2));

			output.at<double>(y, x) = magnitudeVal;
    }
  }

	Mat img;
	img.create(dfdx.size(), CV_64F);

	normalize(output, img, 0, 255, NORM_MINMAX);

  imwrite("output/magnitude.jpg", img);
}

void getDirection(Mat &dfdx, Mat &dfdy,	Mat &output) {
	for (int y = 0; y < output.rows; y++) {
    for (int x = 0; x < output.cols; x++) {

			double dxVal = 0.0;
      double dyVal = 0.0;
      double gradientVal = 0.0;

      dxVal = dfdx.at<double>(y, x);
      dyVal = dfdy.at<double>(y, x);

			// Calculate direction
			if (dxVal != 0 && dyVal != 0) gradientVal = atan2(dyVal, dxVal);
			else gradientVal = (double) atan(0);

			output.at<double>(y, x) = gradientVal;
    }
  }

	Mat img;
	img.create(dfdx.size(), CV_64F);

	normalize(output, img, 0, 255, NORM_MINMAX);

	imwrite("output/direction.jpg", img);
}

void getThresholdedMag(Mat &input, Mat &output) {
	Mat img;
	img.create(input.size(), CV_64F);

	normalize(input, img, 0, 255, NORM_MINMAX);

	for (int y = 0; y < input.rows; y++) {
    for (int x = 0; x < input.cols; x++) {

      double val = 0;
      val = img.at<double>(y, x);


      if (val > 200) output.at<double>(y, x) = 255.0;
      else output.at<double>(y, x) = 0.0;
    }
  }

  imwrite("output/thresholded.jpg", output);
}

void getHoughSpace( Mat &thresholdedMag, Mat &gradientDirection, int threshold, int width, int height, Mat &houghSpace, std::vector<double> &rhoValues, std::vector<double> &thetaValues) {
	//double maxDist = sqrt(pow(width, 2) + pow(height, 2)) / 2;

	double rho = 0.0;
	double radians = 0.0;
	double directionTheta = 0.0;
	double directionVal = 0.0;
	int angleRange = 1;

	// houghSpace.create(round(maxDist), 180, CV_64F);

	houghSpace.create(2*(width + height), 360, CV_64F);

  houghSpace = Scalar(0,0,0);

	for (int y = 0; y < thresholdedMag.rows; y++) {
		for (int x = 0; x < thresholdedMag.cols; x++) {
			if (thresholdedMag.at<double>(y, x) > 250) {

				directionVal = gradientDirection.at<double>(y, x);
				if (directionVal > 0) directionTheta = (directionVal * (180/PI));
				else directionTheta = 360 + (directionVal * (180/PI));

				directionTheta = round(directionTheta);

				for (int theta = directionTheta - angleRange; theta < directionTheta + angleRange + 1; theta += 1) {
				// for (int theta = 0; theta < 360; theta++){
					radians = theta * (PI/ 180);

					rho = (x * cos(radians)) + (y * sin(radians)) + width + height;

					houghSpace.at<double>( rho , theta )++;
				}
			}
		}
	}

	normalize(houghSpace, houghSpace, 0, 255, NORM_MINMAX);

	double min, max;
	cv::minMaxLoc(houghSpace, &min, &max);
	// double houghSpaceThreshold = min + ((max - min)/2);

	// std::cout << max << " and " << min << '\n';

	// Thresholding Hough space
	for (int y = 0; y < houghSpace.rows; y++) {
		for (int x = 0; x < houghSpace.cols; x++) {

			double val = 0.0;
      val = houghSpace.at<double>(y, x);

			if (val > 150){
				rhoValues.push_back(y);
				thetaValues.push_back(x);
				houghSpace.at<double>(y, x) = 255;
				// std::cout<< rhoValues.size() << " + " << thetaValues.size() << "\n";
			}

      else houghSpace.at<double>(y, x) = 0.0;
		}
	}

	imwrite("output/houghSpace.jpg", houghSpace);
}

void extractLines( Mat &frame, Mat &croppedImg, Rect box, std::vector<double> &rhoValues, std::vector<double> &thetaValues, vector<lineData> &lines ){

  int frameWidth = frame.cols;
  int frameHeight = frame.rows;

  int cropWidth = croppedImg.cols;
  int cropHeight = croppedImg.rows;

  Mat image = frame;

	for (int i = 0; i < rhoValues.size(); i++) {

		Point point1, point2;
		double theta = thetaValues[i];
		double rho = rhoValues[i];
    double m, c;
		double radians = theta * (PI/ 180);

    if (thetaValues.size() < 100) {
      // Using y = mx + c
      // y = (p/sin(theta)) - x(cos(theta)/sin(theta))
      m = cos(radians) / sin(radians);
      c = (rho - cropWidth - cropHeight)/sin(radians);

      // When x = box.x and y = box.y + height
      point1.x = cvRound(box.x);
      point1.y = cvRound(box.y) + cvRound(c);
      // When x = end of image
      point2.x = cvRound(box.x + box.width);
      point2.y = cvRound(box.y) + cvRound(c - (box.width * m));

      clipLine(box, point1, point2);

      lineData currentLine (m, c, thetaValues[i], rhoValues[i], point1, point2);

      lines.push_back(currentLine);
    }
	}
}

vector<Vec3f> extractCircles( Mat &frame ) {

  vector<Vec3f> circles;

  Mat kernel = (Mat_<double>(3,3) << -1,-1,-1,
																		 -1, 9,-1,
																		 -1,-1,-1);

  filter2D(frame, frame, -1, kernel);
  GaussianBlur(frame, frame, Size(3,3), 0, 0, BORDER_DEFAULT);
  HoughCircles( frame, circles, CV_HOUGH_GRADIENT, 1, frame.rows/4, 250, 50, 0, 0 );

  for (int i = 0; i < circles.size(); i++) {
    Vec3f circle = circles[i];
    allCircles.push_back(circle);
  }

  return circles;
}

vector<lineData> filterLines( vector<lineData> &lines, int tolerance ) {
  vector<lineData> filteredLines;

  // Sort lines by theta
  std::sort(lines.begin(), lines.end());

  // Initialise filteredLines with first line
  filteredLines.push_back(lines[0]);
  int compareTo = 0;

  for (int i = 1; i < lines.size(); i++) {
    double previousLineTheta = lines[compareTo].theta;
    double currentLineTheta = lines[i].theta;
    double thetaDiff = currentLineTheta - previousLineTheta;
    // Check if current theta is greater than tolerance since last line theta
    if (thetaDiff > tolerance ) {
      filteredLines.push_back(lines[i]);
      compareTo = i;
    }
  }

  return filteredLines;
}

void drawLines( Mat &frame, vector<lineData> &lines ) {
  for (int i = 0; i < lines.size(); i++) {
    Point point1 = lines[i].point1;
    Point point2 = lines[i].point2;
    line(frame, point1, point2,  Scalar( 255, 0, 0 ), 1);
  }
}

Rect reduceBox( Rect box ){

  double originalArea = box.width * box.height;
  double halfArea = originalArea/4;
  double newLength = cvRound(sqrt(halfArea));

  double centreX = box.x + (box.width/2);
  double centreY = box.y + (box.height/2);

  double newCentreX = cvRound(centreX - (newLength/2));
  double newCentreY = cvRound(centreY - (newLength/2));

  Rect newBox(newCentreX, newCentreY, newLength, newLength);

  return newBox;
}

void detectionDecision( Mat &frame, Mat &croppedImg, Rect box, vector<lineData> &lines, vector<Vec3f> &circles) {

  rectangle(frame, Point(box.x, box.y), Point(box.x + box.width, box.y + box.height), Scalar( 0, 255, 0 ), 2);

  // Find smaller search space of 25% of original box with centred axis
  Rect reducedBox = reduceBox(box);
  vector<Point> midpoints;
  int midpointCounter = 0;
  int circleCounter = 0;

  int boxWidthBound = reducedBox.x + reducedBox.width;
  int boxHeightBound = reducedBox.y + reducedBox.height;

  // Check how many midpoints of lines are in smaller box
  if (lines.size() > 1) {
    for (int i = 0; i < lines.size(); i++) {
      lineData line = lines[i];
      double midpointX = (line.point1.x + line.point2.x)/2;
      double midpointY = (line.point1.y + line.point2.y)/2;

      bool xInBox = midpointX >= reducedBox.x && midpointX <= boxWidthBound;
      bool yInBox = midpointY >= reducedBox.y && midpointY <= boxHeightBound;

      if (xInBox && yInBox) midpointCounter++;
    }

    // bool circleInBox;

    // if (midpointCounter > 3) {
    //   detectedDartboardsFinal.push_back(box);
    // }
  }

  // Check how many circle centres fall in smaller box
  if (circles.size() > 0) {
    for (int i = 0; i < circles.size(); i++) {
      Vec3f circle = circles[i];

      double circleCentreX = circle[0] + box.x;
      double circleCentreY = circle[1] + box.y;

      bool xInBox = circleCentreX >= reducedBox.x && circleCentreX <= boxWidthBound;
      bool yInBox = circleCentreY >= reducedBox.y && circleCentreY <= boxHeightBound;


      if (xInBox && yInBox){
        circleCounter++;
      }
    }
  }

  // Check simply for a few lines to validate with when using circles

  if (midpointCounter > 4) {
    detectedDartboardsFinal.push_back(box);
  }
  else {
    if (circleCounter > 0 && lines.size() > 0)
      detectedDartboardsFinal.push_back(box);
  }

  rectangle(frame, Point(reducedBox.x, reducedBox.y), Point(reducedBox.x + reducedBox.width, reducedBox.y + reducedBox.height), Scalar( 0, 102, 0 ), 2);
}

void dartboardDetector( Mat &frame, Mat &drawingFrame, Mat &croppedImg, Rect box ) {

  Mat image = croppedImg;

  Mat grayImage;

  cvtColor( image, grayImage, CV_BGR2GRAY );
  // equalizeHist( grayImage, grayImage );

  GaussianBlur(grayImage, grayImage, Size(3,3), 0, 0, BORDER_DEFAULT);

  Mat dfdx;
  dfdx.create(image.size(), CV_64F);

  Mat dfdy;
  dfdy.create(image.size(), CV_64F);

	Mat dxKernel = (Mat_<double>(3,3) << -1, 0, 1,
																			 -2, 0, 2,
																			 -1, 0, 1);

  Mat dyKernel = (Mat_<double>(3,3) << -1,-2,-1,
																		    0, 0, 0,
																		    1, 2, 1);

  Mat gradientMagnitude;
  gradientMagnitude.create(image.size(), CV_64F);

  Mat gradientDirection;
	gradientDirection.create(image.size(), CV_64F);

	Mat thresholdedMag;
	thresholdedMag.create(image.size(), CV_64F);

	Mat houghSpace;

  Mat circleFrame = grayImage.clone();

  std::vector<double> rhoValues;
  std::vector<double> thetaValues;

  vector<lineData> lines;
  vector<lineData> filteredLines;

  vector<Vec3f> circles;

  convolution(grayImage, 3, 0, dxKernel, dfdx);
  convolution(grayImage, 3, 1, dyKernel, dfdy);

	getMagnitude(dfdx, dfdy, gradientMagnitude);
	getDirection(dfdx, dfdy, gradientDirection);

	getThresholdedMag(gradientMagnitude, thresholdedMag);

  imwrite("output/thresholdedMag.jpg", thresholdedMag);

	getHoughSpace(thresholdedMag, gradientDirection, 240, image.cols, image.rows, houghSpace, rhoValues, thetaValues);

  circles = extractCircles( circleFrame );

	extractLines(frame, image, box, rhoValues, thetaValues, lines);

  if (lines.size() > 1)
    filteredLines = filterLines(lines, 20);

  detectionDecision(drawingFrame, image, box, filteredLines, circles);

  drawLines( drawingFrame, filteredLines);

  for( int i = 0; i < circles.size(); i++ )
  {
     Point center(cvRound(circles[i][0] + box.x), cvRound(circles[i][1] + box.y));
     int radius = cvRound(circles[i][2]);
     // circle center
     circle( drawingFrame, center, 3, Scalar(0,255,0), -1, 8, 0 );
     // circle outline
     circle( drawingFrame, center, radius, Scalar(0,0,255), 2, 8, 0 );
   }

  imwrite("output/foundLines.jpg", drawingFrame);

}

float F1Test( int facesDetected, const char* imgName, Mat frame, int detectorChoice ){
	int validFaces = 0;
  std::vector<Rect> trueDartboards;

	// Manipulate string to get correct CSV file name
	string fileExtension = "points.csv";
	string current_line;

	std::string imgNameString(imgName);

	string::size_type i = imgNameString.rfind('.', imgNameString.length());
  if (i != string::npos) {
		imgNameString.replace(i, fileExtension.length(), fileExtension);
  }

	const char *c = imgNameString.c_str();
  string prePath = "CSVs/dartboards/";
  string name = imgNameString.c_str();
  string newName = prePath + name;

  ifstream inputFile;
  inputFile.open(newName.c_str());

	// Break if no CSV file found
	if (inputFile.peek() == std::ifstream::traits_type::eof()) {
		std::cout << "No CSV file found. F1 score cannot be calculated" << '\n';
		return 0.0;
	}

	// Go through CSV file line by line
	while(getline(inputFile, current_line)){

		// Array of values for each box
		std::vector<int> values;

		std::stringstream convertor(current_line);
		std::string token; // somewhere to put the comma separated value

		// Insert each value into values array
		while (std::getline(convertor, token, ',')) {
			values.push_back(std::atoi(token.c_str()));
		}

		// Populate array with ground truth rectangles
		trueDartboards.push_back(Rect(values[0], values[1], values[2], values[3]));
	}

  std::vector<Rect> detectedDartboards;

  if (detectorChoice == 0) detectedDartboards = detectedDartboardsVJ;
  else                     detectedDartboards = detectedDartboardsFinal;

  int truePositives = 0;
	int falsePositives = 0;

	// Compare each detected face to every ground truth face
	for (int i = 0; i < detectedDartboards.size(); i++) {
		for (int j = 0; j < trueDartboards.size(); j++) {
			// Get intersection and check matching area percentage
			Rect intersection = detectedDartboards[i] & trueDartboards[j];
			float intersectionArea = intersection.area();

			// If there is an intersection, check percentage of intersection area
			// to detection area
			if (intersectionArea > 0) {
				float matchPercentage = (intersectionArea / trueDartboards[j].area()) * 100;

				// If threshold reached, increment true positives
				if (matchPercentage > 60){
					truePositives++;
					break;
				}
				if (j == (trueDartboards.size() - 1)) falsePositives++;
			}
			// If loop reaches end without reaching intersection threshold, it is
			// a false negative
			else {
				if (j == (trueDartboards.size() - 1)) falsePositives++;
			}
		}
	}

  if (detectorChoice == 0) std::cout << "############ VIOLA JONES RESULTS ############" << '\n';
  else                     std::cout << "########### FINAL DETECTOR RESULTS ###########" << '\n';

  if (detectorChoice == 0)
    std::cout << "Dartboards detected with Viola Jones: " << detectedDartboardsVJ.size() << std::endl;
  else
    std::cout << "Dartboards detected with Hough: " <<detectedDartboardsFinal.size() << '\n';

	std::cout << "True Dartboards: " << trueDartboards.size() << ", True Positives: " << truePositives << ", False Positives: " << falsePositives << "\n";

	// Time for F1 test
	// Precision = TP / (TP + FP)
	// Recall = TPR (True Positive Rate)
	// F1 = 2((PRE * REC)/(PRE + REC))

	float precision = (float)truePositives / ((float)truePositives + (float)falsePositives);
	float recall = (float)truePositives / (float)trueDartboards.size();

  if (isnan(precision)) precision = 0;

  std::cout << "Precision: " << precision << " and Recall: " << recall << '\n';

	float f1 = 2 * ((precision * recall)/(precision + recall));

  if (isnan(f1)) f1 = 0;

	std::cout << "F1 score: " << f1 << "\n";

	return f1;
}

void detectVJ( Mat frame ){
	Mat frame_gray;

	// 1. Prepare Image by turning it into Grayscale and normalising lighting
	cvtColor( frame, frame_gray, CV_BGR2GRAY );
	equalizeHist( frame_gray, frame_gray );

	// 2. Perform Viola-Jones Object Detection
	cascade.detectMultiScale( frame_gray, detectedDartboardsVJ, 1.1, 1, 0|CV_HAAR_SCALE_IMAGE, Size(50, 50), Size(500,500) );

}

void displayVJ( Mat frame ){
  // Draw box around dartboards found with Viola Jones
	for( int i = 0; i < detectedDartboardsVJ.size(); i++ )
	{
		rectangle(frame, Point(detectedDartboardsVJ[i].x, detectedDartboardsVJ[i].y), Point(detectedDartboardsVJ[i].x + detectedDartboardsVJ[i].width, detectedDartboardsVJ[i].y + detectedDartboardsVJ[i].height), Scalar( 0, 255, 0 ), 2);
	}
}

void displayHough( Mat frame ){
  // Draw box around dartboards found with Viola Jones + hough
  for( int i = 0; i < detectedDartboardsFinal.size(); i++ )
  {
    rectangle(frame, Point(detectedDartboardsFinal[i].x, detectedDartboardsFinal[i].y), Point(detectedDartboardsFinal[i].x + detectedDartboardsFinal[i].width, detectedDartboardsFinal[i].y + detectedDartboardsFinal[i].height), Scalar( 0, 255, 0 ), 2);
  }
}

void analyseBoxes( Mat &frame ){

  // Frame passed around to draw found lines, boxes and circles on
  Mat drawingFrame = frame.clone();

  // Decide whether there is dartboard in each cropped image
  for (int i = 0; i < detectedDartboardsVJ.size(); i ++){
    Mat frameForCrop = frame.clone();
    Mat croppedImg = frameForCrop(detectedDartboardsVJ[i]);
    dartboardDetector(frame, drawingFrame, croppedImg, detectedDartboardsVJ[i]);
  }
}

std::vector<Rect> mergeDetections() {
  std::vector<Rect> newDartboardDetections;
  std::vector<int> checked;

  for (int i = 0; i < detectedDartboardsFinal.size(); i++) {
		for (int j = 1; j < detectedDartboardsFinal.size(); j++) {

      //if(std::find(checked.begin(), checked.end(), j) != checked.end()) continue;
      if(std::find(checked.begin(), checked.end(), i) != checked.end()) break;

      if (i == detectedDartboardsFinal.size() - 1)
        newDartboardDetections.push_back(detectedDartboardsFinal[i]);

      if (j <= i) continue;

      Rect box1 = detectedDartboardsFinal[i];
      Rect box2 = detectedDartboardsFinal[j];

			// Get intersection and check matching area percentage
			Rect intersection = detectedDartboardsFinal[i] & detectedDartboardsFinal[j];
			float intersectionArea = intersection.area();

			// If there is an intersection, check percentage of intersection area
			if (intersectionArea > 0) {
				float matchPercentage = (intersectionArea / detectedDartboardsFinal[i].area()) * 100;

        //std::cout << matchPercentage << '\n';

        int newX = cvRound((box1.x + box2.x) / 2);
        int newY = cvRound((box1.y + box2.y) / 2);
        int newWidth = cvRound((box1.width + box2.width) / 2);
        int newHeight = cvRound((box1.height + box2.height) / 2);

				// If threshold reached, merge rectangles by getting average of points
				if (matchPercentage > 60){

          Rect newBox(newX, newY, box2.width, box2.height);

          newDartboardDetections.push_back(newBox);
          checked.push_back(i);
          checked.push_back(j);
          break;
				}
        if (j == (detectedDartboardsFinal.size() - 1)){
          newDartboardDetections.push_back(box1);
          checked.push_back(i);
        }
			}
			// If loop reaches end without reaching intersection threshold, it is
			// a false negative
			else {
        // std::cout << "i: " << i << " and j: " << j << '\n';
				if (j == (detectedDartboardsFinal.size() - 1)){
          newDartboardDetections.push_back(box1);
          checked.push_back(i);
        }
			}
		}
  }
  return newDartboardDetections;
}

Mat grabCut( Mat frame, int view ) {
  Rect box = detectedDartboardsFinal[view];

  Mat result;
  Mat bgModel, fgModel;

  // Use grabCut algorithm to segment background from foreground
  grabCut(frame, result, box, bgModel, fgModel, 10, GC_INIT_WITH_RECT );

  // Find foreground pixels
  compare(result, cv::GC_PR_FGD, result, cv::CMP_EQ);

  // Generate output image
  cv::Mat foreground(frame.size(), CV_8UC3, Scalar(0,0,0));
  frame.copyTo(foreground, result);

  imwrite("output/foreground.jpg", foreground);

  return foreground;
}

void outputWindow( Mat frame ) {

  Mat detectedFeatures = imread("output/foundLines.jpg", CV_LOAD_IMAGE_COLOR);
  Mat finalOutput = imread("output/detected.jpg", CV_LOAD_IMAGE_COLOR);

  std::vector<Mat> segmentedImages;

  for (int i = 0; i < detectedDartboardsFinal.size(); i++) {
    Mat grabCutImage = grabCut(frame, i);
    segmentedImages.push_back(grabCutImage);
  }

  namedWindow("Viola Jones and Hough Features Detected", CV_WINDOW_AUTOSIZE);
  imshow("Viola Jones and Hough Features Detected", detectedFeatures);

  waitKey(0);

  namedWindow("Final Detections", CV_WINDOW_AUTOSIZE);
  imshow("Final Detections", finalOutput);

  waitKey(0);

  namedWindow("Segmented Dartboards", CV_WINDOW_AUTOSIZE);

  for (int i = 0; i < segmentedImages.size(); i++) {
    imshow("Segmented Dartboards", segmentedImages[i]);
    waitKey(0);
  }
}

int main( int argc, const char** argv ){

	const char* imgName = argv[1];

  // 1. Read Input Image
	Mat frame = imread(argv[1], CV_LOAD_IMAGE_COLOR);
  Mat VJFrame = frame.clone();
  Mat houghFrame = frame.clone();
  Mat finalOutput = frame.clone();
  Mat grabCutFrame = frame.clone();

	// 2. Load the Strong Classifier in a structure called `Cascade'
	if( !cascade.load( cascade_name ) ){ printf("--(!)Error loading\n"); return -1; };

	// 3. Detect Faces with Viola Jones
	detectVJ( VJFrame );

  // 4. Draw Viola Jones boxes
  displayVJ( VJFrame );

  // 5. Save image with Viola Jones detections
  imwrite( "output/detectedVJ.jpg", VJFrame );

  // 6. Loop through cropped boxes to detect lines and decide if dartboard or not
  analyseBoxes( houghFrame );

  // 7. Merge overlapping true positives into one
  if (detectedDartboardsFinal.size() > 1)
    detectedDartboardsFinal = mergeDetections();

  // 8. Show and save final dartboard detections
  if (detectedDartboardsFinal.size() > 0)
    displayHough( finalOutput );

  // 9. Save Result Image
  imwrite("output/detected.jpg", finalOutput);

  // 10. Create window with trackbar showing GrabCut foregrounds
  outputWindow(grabCutFrame);

	// 11. Perform F1 test
	float f1ScoreVJ = F1Test(detectedDartboardsVJ.size(), imgName, frame, 0);
  float f1ScoreFinal = F1Test(detectedDartboardsFinal.size(), imgName, frame, 1);

	return 0;
}
