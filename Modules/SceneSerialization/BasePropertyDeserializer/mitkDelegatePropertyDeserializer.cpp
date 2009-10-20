/*=========================================================================
 
Program:   Medical Imaging & Stringeraction Toolkit
Module:    $RCSfile: mitkPropertyManager.cpp,v $
Language:  C++
Date:      $Date: 2009-10-08 16:58:56 +0200 (Do, 08. Okt 2009) $
Version:   $Revision: 1.12 $
 
Copyright (c) German Cancer Research Center, Division of Medical and
Biological Informatics. All rights reserved.
See MITKCopyright.txt or http://www.mitk.org/copyright.html for details.
 
This software is distributed WITHOUT ANY WARRANTY; without even
the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
PURPOSE.  See the above copyright notices for more information.
 
=========================================================================*/

#ifndef mitkDelegatePropertyDeserializer_h_included
#define mitkDelegatePropertyDeserializer_h_included

#include "mitkBasePropertyDeserializer.h"

#include "mitkDelegateProperty.h"

namespace mitk
{

class SceneSerialization_EXPORT DelegatePropertyDeserializer : public BasePropertyDeserializer
{
  public:
    
    mitkClassMacro( DelegatePropertyDeserializer, BasePropertyDeserializer );
    itkNewMacro(Self);

    virtual BaseProperty::Pointer Deserialize(TiXmlElement* element)
    {
      if (!element) return NULL;
      const char* s( element->Attribute("value") );
      return DelegateProperty::New( std::string(s?s:"") ).GetPointer();
    }

  protected:

    DelegatePropertyDeserializer() {}
    virtual ~DelegatePropertyDeserializer() {}
};

} // namespace

// important to put this into the GLOBAL namespace (because it starts with 'namespace mitk')
MITK_REGISTER_SERIALIZER(DelegatePropertyDeserializer);

#endif

