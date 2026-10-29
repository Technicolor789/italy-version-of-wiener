FROM php:8.3-apache

# zip: to read static GTFS files (Trenord and ATM timetables)
RUN apt-get update \
 && apt-get install -y --no-install-recommends libzip-dev \
 && docker-php-ext-install zip \
 && rm -rf /var/lib/apt/lists/*

COPY . /var/www/html/

# Import the timetables at build time, so the first request does not wait.
# gtfs/<feed>.zip from the repo is used when present; Trenord can also be
# downloaded, and the proxy refreshes it by itself every week.
RUN cd /var/www/html && mkdir -p data \
 && for feed in trenord atm; do \
      if [ -f gtfs/$feed.zip ]; then php gtfs_static.php build $feed gtfs/$feed.zip; \
      else php gtfs_static.php build $feed; fi || echo "$feed timetable not built now"; \
    done \
 && for feed in toscana; do \
      [ -d gtfs/$feed ] && php gtfs_names.php build $feed gtfs/$feed || echo "$feed names not built"; \
    done \
 && rm -rf gtfs \
 && chown -R www-data:www-data data
