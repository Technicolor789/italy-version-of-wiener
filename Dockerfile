FROM php:8.3-apache

# zip: to read static GTFS files (Trenord timetables)
RUN apt-get update \
 && apt-get install -y --no-install-recommends libzip-dev \
 && docker-php-ext-install zip \
 && rm -rf /var/lib/apt/lists/*

COPY . /var/www/html/

# Import the Trenord timetable at build time, so the first request does not
# wait for it. A zip committed in gtfs/trenord.zip is used if present. If the
# download fails the proxy builds it on the first request instead.
RUN cd /var/www/html \
 && (if [ -f gtfs/trenord.zip ]; then php gtfs_static.php build trenord gtfs/trenord.zip; \
     else php gtfs_static.php build trenord; fi || echo "Trenord timetable not built now") \
 && mkdir -p data && chown -R www-data:www-data data
