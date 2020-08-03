# Analysis Tools

These tools where used to create the `spring-boot` and `customer2` performance reports. The code is written in Python 3, and encapsulated inside Jupyter notebook files (`.ipynb`). Jupyter notebooks are a mixture of Markdown and Python 3. They are used widely in the data science community as an interactive 'poking around' environment.

customer2.ipynb
: Source for initial performance report on Customer2 examples.
Service_Startup_Performance.ipynb
: Source for Service Startup Report
StripChart.ipynb
: Time sequence strip charts based on KM hypercall traces.

To convert a `.ipynb` file to Markdown, use `jupyter-nbconvert docs/analysis/customer2.ipynb --to markdown`. There are VS-Code plugins that claim to support `.ipynb` files, but I use `jupyter notebook` which is browser based.