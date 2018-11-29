#include "Shape.h"

Shape::Shape(const vector<Point> & contour) {
   this->contour = contour;

   // eliminate small blobs
   if(getArea() < 100) {
        isValidShape = false;
        return;
   }
   // eliminate concave blobs
   if(!isContourConvex(getApprox())){
         isValidShape = false;
         return;
   }
}

double Shape::getArea(){
   if(area == -1){
      area = contourArea(contour);
   }
   return area;
}

double Shape::getPerimeter(){
   if(perimeter == -1){
      perimeter = arcLength(contour,true);
   }
   return perimeter;
}

Point2f Shape::getCenter(){
   if(center.x == FLT_MAX){
      Moments mu = moments( contour, false);
      center = Point2f( mu.m10/mu.m00 , mu.m01/mu.m00 );
   }
   return center;
}

Rect Shape::getBoundingRect(){
   if(boundingRect.empty()){
      boundingRect = cv::boundingRect(contour);
   }
   return boundingRect;
}

vector<Point> Shape::getApprox(){
   if(approx.empty()){
      double epsilon = 0.02*getPerimeter();
      approx = approximatePolyDP(epsilon);
   }
   return approx;
}

string Shape::getType(){
   if(type == "NULL"){
      if (getApprox().size() == 3){
         type = "TRI";
      }
      else if (getApprox().size() == 4){
         type = "RECT";
      }
      else{
         int radius = getBoundingRect().width / 2;
         if (abs(1 - ((double)boundingRect.width / boundingRect.height)) <= 0.2 &&
            abs(1 - (area / (CV_PI * pow(radius, 2)))) <= 0.2){
               type = "CIR";
         }
         else{
            type = "OTR";
         }
      }
   }
   return type;
}

vector<Point> Shape::approximatePolyDP(double epsilon){
   approxPolyDP(contour, approx, epsilon, true);
   return approx;
}

void Shape::draw(cv::Mat& image){
   auto color = isValidShape ? Scalar(255,0,0): Scalar(0,0,255);
   polylines(image, contour, true, color, 1);

   if(isValidShape){
      int fontface = cv::FONT_HERSHEY_SIMPLEX;
      double scale = 0.4;
      int thickness = 1;
      int baseline = 0;

      string label = getType();
      cv::Size text = cv::getTextSize(label, fontface, scale, thickness, &baseline);
      cv::Rect r = getBoundingRect();
      cv::Point pt(r.x + ((r.width - text.width) / 2), r.y + ((r.height + text.height) / 2));

      for( unsigned int j = 0; j< getApprox().size(); j++ ){
           circle( image, approx[j], 2, Scalar(0,255,0), -1, 8, 0 );
      }
      cv::rectangle(image, pt + cv::Point(0, baseline), pt + cv::Point(text.width, -text.height),Scalar(255,255,255), CV_FILLED);
      cv::putText(image, label, pt, fontface, scale, CV_RGB(0,0,0), thickness, 8);
   }else{
      circle( image, getCenter(),2, color, -1, 8, 0 );
   }

}
