[![Build Status](https://mscodehub.visualstudio.com/Azile/_apis/build/status/GitHub-JsonBuilder-CI?branchName=master)](https://mscodehub.visualstudio.com/Azile/_build/latest?definitionId=922&branchName=master)

# JsonBuilder

JsonBuilder is a small C++ library for building a space-efficient binary representation of structured data and, when ready, rendering it to JSON. The library offers STL-like syntax for adding and finding data as well as STL-like iterators for efficiently tracking location. JsonBuilder is currently used in production projects at Microsoft.

## Integration

JsonBuilder builds as a static library and requires C++11. The project creates a CMake compatible 'jsonbuilder' target which you can use for linking against the library.

## Examples

### Building structured data

Let's try to build the following JSON up using the JsonBuilder interface:

```json
{
    "e": 2.718,
    "enabled": true,
    "user": "john",
    "resolution": {
        "x": 1024,
        "y": 768
    },
    "colors": [
        "Red",
        "Green",
        "Blue"
    ]
}
```

The code to do so would look like this:

```cpp
JsonBuilder jb;
jb.push_back(jb.end(), "e", 2.718);
jb.push_back(jb.end(), "enabled", true);
jb.push_back(jb.end(), "user", "john");

JsonIterator resolutionItr = jb.push_back(jb.end(), "resolution", JsonObject);
jb.push_back(resolutionItr, "x", 1024);
jb.push_back(resolutionItr, "y", 768);

JsonIterator colorIterator = jb.push_back(jb.end(), "colors", JsonArray);
jb.push_back(colorIterator, "", "Red");
jb.push_back(colorIterator, "", "Green");
jb.push_back(colorIterator, "", "Blue");
```

### Getting an iterator to existing data

Using the built JsonBuilder object above as a starting point:

```cpp
// Float
JsonConstIterator eItr = jb.find("e");
float e = eItr->GetUnchecked<float>();
std::cout << e << std::endl;

// Object
JsonConstIterator resolutionItr = jb.find("resolution");
for (JsonConstIterator beginItr = resolutionItr.begin(),
                        endItr = resolutionItr.end();
        beginItr != endItr;
        ++beginItr)
{
    std::string name(beginItr->Name().data(), beginItr->Name().length());
    std::cout << name << " " << beginItr->GetUnchecked<int64_t>()
                << std::endl;
}

// Array
JsonConstIterator colorsItr = jb.find("colors");
for (JsonConstIterator beginItr = colorsItr.begin(), endItr = colorsItr.end();
        beginItr != endItr;
        ++beginItr)
{
    auto color = beginItr->GetUnchecked<nonstd::string_view>();
    std::cout << color << std::endl;
}
```

### Rendering to JSON

Using the built JsonBuilder object above as a starting point:

```cpp
// Create a renderer and reserve 2048 bytes up front
JsonRenderer renderer;
renderer.Reserve(2048);

// Render a json builder object to a string
nonstd::string_view result = renderer.Render(_jsonBuilder);
std::string stl_string(result.data(), result.size());
std::cout << stl_string.c_str() << std::endl;
```

## Dependencies

This project carries a dependency on the uuid library. To develop with this project, install the development version of the library:

```bash
sudo apt-get install uuid-dev
```

## Reporting Security Issues

Security issues and bugs should be reported privately, via email, to the
Microsoft Security Response Center (MSRC) at <[secure@microsoft.com](mailto:secure@microsoft.com)>.
You should receive a response within 24 hours. If for some reason you do not, please follow up via
email to ensure we received your original message. Further information, including the
[MSRC PGP](https://technet.microsoft.com/en-us/security/dn606155) key, can be found in the
[Security TechCenter](https://technet.microsoft.com/en-us/security/default).

## Code of Conduct

This project has adopted the [Microsoft Open Source Code of Conduct](https://opensource.microsoft.com/codeofconduct/).

For more information see the [Code of Conduct FAQ](https://opensource.microsoft.com/codeofconduct/faq/) or contact [opencode@microsoft.com](mailto:opencode@microsoft.com) with any additional questions or comments.

## Contributing

Want to contribute? The team encourages community feedback and contributions. Please follow our [contributing guidelines](CONTRIBUTING.md).

We also welcome [issues submitted on GitHub](https://github.com/Microsoft/JsonBuilder/issues).