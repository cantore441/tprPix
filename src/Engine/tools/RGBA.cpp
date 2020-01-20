/*
 * ========================= RGBA.cpp ==========================
 *                          -- tpr --
 *                                        CREATE -- 2020.01.13
 *                                        MODIFY --
 * ----------------------------------------------------------
 */
#include "RGBA.h"

//------------------- CPP --------------------//
#include <iostream>
#include <sstream> //- stringstream


std::string RGBA::to_string()const noexcept{

    std::stringstream ss;

    int r = static_cast<int>(this->r);
    int g = static_cast<int>(this->g);
    int b = static_cast<int>(this->b);
    int a = static_cast<int>(this->a);

    ss << "RGBA{ " << r 
        << ", " << g
        << ", " << b
        << ", " << a 
        << " } ";

    return ss.str();
}





