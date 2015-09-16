/**!
 *
 *  Copyright 2015 Netflix, Inc.
 *
 *     Licensed under the Apache License, Version 2.0 (the "License");
 *     you may not use this file except in compliance with the License.
 *     You may obtain a copy of the License at
 *
 *         http://www.apache.org/licenses/LICENSE-2.0
 *
 *     Unless required by applicable law or agreed to in writing, software
 *     distributed under the License is distributed on an "AS IS" BASIS,
 *     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *     See the License for the specific language governing permissions and
 *     limitations under the License.
 *
 */
!function(){"use strict";function e(e){e.errorClassnames.push("alert-danger")}e.$inject=["flashProvider"],angular.module("app.routes",["ngRoute"]),angular.module("vector.config",[]),angular.module("app.charts",[]),angular.module("app.filters",[]),angular.module("app.metrics",[]),angular.module("app.datamodels",[]),angular.module("app.services",[]),angular.module("app",["app.routes","ui.dashboard","app.controllers","app.datamodels","app.widgets","app.charts","app.services","app.filters","app.metrics","vector.config","angular-flash.service","angular-flash.flash-alert-directive"]).config(e)}(),/**!
 *
 *  Copyright 2015 Netflix, Inc.
 *
 *     Licensed under the Apache License, Version 2.0 (the "License");
 *     you may not use this file except in compliance with the License.
 *     You may obtain a copy of the License at
 *
 *         http://www.apache.org/licenses/LICENSE-2.0
 *
 *     Unless required by applicable law or agreed to in writing, software
 *     distributed under the License is distributed on an "AS IS" BASIS,
 *     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *     See the License for the specific language governing permissions and
 *     limitations under the License.
 *
 */
function(){"use strict";function e(){function e(){return Math.floor(65536*(1+Math.random())).toString(16).substring(1)}return{getGuid:e}}angular.module("app.services").factory("VectorService",e)}(),/**!
 *
 *  Copyright 2015 Netflix, Inc.
 *
 *     Licensed under the Apache License, Version 2.0 (the "License");
 *     you may not use this file except in compliance with the License.
 *     You may obtain a copy of the License at
 *
 *         http://www.apache.org/licenses/LICENSE-2.0
 *
 *     Unless required by applicable law or agreed to in writing, software
 *     distributed under the License is distributed on an "AS IS" BASIS,
 *     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *     See the License for the specific language governing permissions and
 *     limitations under the License.
 *
 */
function(){"use strict";function e(e,t,a,i){function r(t){var r=a.properties.protocol+"://"+a.properties.host+":"+a.properties.port,n={};return n.method="GET",n.url=r+"/pmapi/context",n.params={},n.params[t.contextType]=t.contextValue,n.params.polltimeout=t.pollTimeout.toString(),n.timeout=5e3,e(n).then(function(e){return e.data.context?e.data.context:i.reject("context is undefined")})}function n(e,t){var a={};return a.contextType="hostspec",a.contextValue=e,a.pollTimeout=t,r(a)}function o(e,t){var a={};return a.contextType="hostname",a.contextValue=e,a.pollTimeout=t,r(a)}function s(e){var t={};return t.contextType="local",t.contextValue="ANYTHING",t.pollTimeout=e,r(t)}function c(e,t){var a={};return a.contextType="archivefile",a.contextValue=e,a.pollTimeout=t,r(a)}function l(t,r,n){var o=a.properties.protocol+"://"+a.properties.host+":"+a.properties.port,s={};return s.method="GET",s.url=o+"/pmapi/"+t+"/_fetch",s.params={},angular.isDefined(r)&&null!==r&&(s.params.names=r.join(",")),angular.isDefined(n)&&null!==n&&(s.params.pmids=n.join(",")),e(s).then(function(e){return angular.isUndefined(e.data.timestamp)||angular.isUndefined(e.data.timestamp.s)||angular.isUndefined(e.data.timestamp.us)||angular.isUndefined(e.data.values)?i.reject("metric values is empty"):e})}function d(t,r,n,o){var s=a.properties.protocol+"://"+a.properties.host+":"+a.properties.port,c={};return c.method="GET",c.url=s+"/pmapi/"+t+"/_indom",c.params={indom:r},angular.isDefined(n)&&null!==n&&(c.params.instance=n.join(",")),angular.isDefined(o)&&null!==o&&(c.params.inames=o.join(",")),c.cache=!0,e(c).then(function(e){return angular.isDefined(e.data.indom)||angular.isDefined(e.data.instances)?e:i.reject("instances is undefined")})}function p(t,r,n,o){var s=a.properties.protocol+"://"+a.properties.host+":"+a.properties.port,c={};return c.method="GET",c.url=s+"/pmapi/"+t+"/_indom",c.params={name:r},angular.isDefined(n)&&null!==n&&(c.params.instance=n.join(",")),angular.isDefined(o)&&null!==o&&(c.params.inames=o.join(",")),c.cache=!0,e(c).then(function(e){return angular.isDefined(e.data.instances)?e:i.reject("instances is undefined")})}function u(e){var t=1e3*e.data.timestamp.s+e.data.timestamp.us/1e3,a=e.data.values;return{timestamp:t,values:a}}function m(e){var t={};return angular.forEach(e,function(e){var a=e.data.indom,i=e.config.params.name,r={};angular.forEach(e.data.instances,function(e){r[e.instance.toString()]=e.name}),t[i.toString()]={indom:a,name:i,inames:r}}),t}function g(e,t){var a=i.defer(),r=[];return angular.forEach(t.values,function(t){var a=_.map(t.instances,function(e){return angular.isDefined(e.instance)&&null!==e.instance?e.instance:-1});r.push(p(e,t.name,a))}),i.all(r).then(function(e){var i=m(e),r={timestamp:t.timestamp,values:t.values,inames:i};a.resolve(r)},function(e){a.reject(e)},function(e){a.update(e)}),a.promise}function h(e,t){return l(e,t).then(u).then(function(t){return g(e,t)})}return{getHostspecContext:n,getHostnameContext:o,getLocalContext:s,getArchiveContext:c,getMetricsValues:l,getMetrics:h,getInstanceDomainsByIndom:d,getInstanceDomainsByName:p}}angular.module("app.services").factory("PMAPIService",e),e.$inject=["$http","$log","$rootScope","$q"]}(),/**!
 *
 *  Copyright 2015 Netflix, Inc.
 *
 *     Licensed under the Apache License, Version 2.0 (the "License");
 *     you may not use this file except in compliance with the License.
 *     You may obtain a copy of the License at
 *
 *         http://www.apache.org/licenses/LICENSE-2.0
 *
 *     Unless required by applicable law or agreed to in writing, software
 *     distributed under the License is distributed on an "AS IS" BASIS,
 *     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *     See the License for the specific language governing permissions and
 *     limitations under the License.
 *
 */
function(){"use strict";function e(e,t,a,i,r,n,o,s,c,l,d){function p(e){var t=_.find(k,function(t){return t.name===e});return angular.isUndefined(t)?(t=new n(e),k.push(t)):t.subscribers++,t}function u(e){var t=_.find(k,function(t){return t.name===e});return angular.isUndefined(t)?(t=new o(e),k.push(t)):t.subscribers++,t}function m(e,t){var a=_.find(k,function(t){return t.name===e});return angular.isUndefined(a)?(a=new s(e,t),k.push(a)):a.subscribers++,a}function g(e,t){var a=_.find(k,function(t){return t.name===e});return angular.isUndefined(a)?(a=new c(e,t),k.push(a)):a.subscribers++,a}function h(e,t){var a=_.find(x,function(t){return t.name===e});return angular.isUndefined(a)?(a=new l(e,t),x.push(a)):a.subscribers++,a}function f(e){var t,a=_.find(k,function(t){return t.name===e});a.subscribers--,a.subscribers<1&&(t=k.indexOf(a),t>-1&&k.splice(t,1))}function v(e){var t,a=_.find(x,function(t){return t.name===e});a.subscribers--,a.subscribers<1&&(t=x.indexOf(a),t>-1&&x.splice(t,1))}function y(){angular.forEach(k,function(e){e.clearData()})}function w(){angular.forEach(x,function(e){e.clearData()})}function M(t){var a,i=[],n=e.properties.host,o=e.properties.port,s=e.properties.context,c=e.properties.protocol;s&&s>0&&k.length>0&&(angular.forEach(k,function(e){i.push(e.name)}),a=c+"://"+n+":"+o+"/pmapi/"+s+"/_fetch?names="+i.join(","),r.getMetrics(s,i).then(function(e){angular.forEach(e.values,function(t){var a=t.name;angular.forEach(t.instances,function(t){var i=angular.isUndefined(t.instance)?1:t.instance,r=e.inames[a].inames[i],n=_.find(k,function(e){return e.name===a});angular.isDefined(n)&&null!==n&&n.pushValue(e.timestamp,i,r,t.value)})})}).then(function(){t(!0)},function(){d.to("alert-dashboard-error").error="Failed fetching metrics.",t(!1)}),e.$broadcast("updateMetrics"))}function b(){x.length>0&&(angular.forEach(x,function(e){e.updateValues()}),e.$broadcast("updateDerivedMetrics"))}var k=[],x=[];return{getOrCreateMetric:p,getOrCreateCumulativeMetric:u,getOrCreateConvertedMetric:m,getOrCreateCumulativeConvertedMetric:g,getOrCreateDerivedMetric:h,destroyMetric:f,destroyDerivedMetric:v,clearMetricList:y,clearDerivedMetricList:w,updateMetrics:M,updateDerivedMetrics:b}}e.$inject=["$rootScope","$http","$log","$q","PMAPIService","SimpleMetric","CumulativeMetric","ConvertedMetric","CumulativeConvertedMetric","DerivedMetric","flash"],angular.module("app.services").factory("MetricListService",e)}(),/**!
 *
 *  Copyright 2015 Netflix, Inc.
 *
 *     Licensed under the Apache License, Version 2.0 (the "License");
 *     you may not use this file except in compliance with the License.
 *     You may obtain a copy of the License at
 *
 *         http://www.apache.org/licenses/LICENSE-2.0
 *
 *     Unless required by applicable law or agreed to in writing, software
 *     distributed under the License is distributed on an "AS IS" BASIS,
 *     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *     See the License for the specific language governing permissions and
 *     limitations under the License.
 *
 */
function(){"use strict";function e(e,t,a){return{getInames:function(e,i){return a.getInstanceDomainsByName(t.properties.context,e,[i])}}}e.$inject=["$http","$rootScope","PMAPIService"],angular.module("app.services").factory("MetricService",e)}(),/**!
 *
 *  Copyright 2015 Netflix, Inc.
 *
 *     Licensed under the Apache License, Version 2.0 (the "License");
 *     you may not use this file except in compliance with the License.
 *     You may obtain a copy of the License at
 *
 *         http://www.apache.org/licenses/LICENSE-2.0
 *
 *     Unless required by applicable law or agreed to in writing, software
 *     distributed under the License is distributed on an "AS IS" BASIS,
 *     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *     See the License for the specific language governing permissions and
 *     limitations under the License.
 *
 */
function(){"use strict";function e(e,t,a,i){function r(){a.get(t.properties.protocol+"://"+t.properties.host+":"+t.properties.port+"/pmapi/"+t.properties.context+"/_fetch?names=generic.heatmap").success(function(){i.to("alert-disklatency-success").success="generic.heatmap requested!",e.info("generic.heatmap requested")}).error(function(){i.to("alert-disklatency-error").error="failed requesting generic.heatmap!",e.error("failed requesting generic.heatmap")})}return{generate:r}}e.$inject=["$log","$rootScope","$http","flash"],angular.module("app.services").factory("HeatMapService",e)}(),/**!
 *
 *  Copyright 2015 Netflix, Inc.
 *
 *     Licensed under the Apache License, Version 2.0 (the "License");
 *     you may not use this file except in compliance with the License.
 *     You may obtain a copy of the License at
 *
 *         http://www.apache.org/licenses/LICENSE-2.0
 *
 *     Unless required by applicable law or agreed to in writing, software
 *     distributed under the License is distributed on an "AS IS" BASIS,
 *     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *     See the License for the specific language governing permissions and
 *     limitations under the License.
 *
 */
function(){"use strict";function e(e,t,a,i){function r(){a.get(t.properties.protocol+"://"+t.properties.host+":"+t.properties.port+"/pmapi/"+t.properties.context+"/_fetch?names=generic.systack").success(function(){i.to("alert-sysstack-success").success="generic.systack requested!",e.info("generic.systack requested")}).error(function(){i.to("alert-sysstack-error").error="failed requesting generic.systack!",e.error("failed requesting generic.systack")})}return{generate:r}}e.$inject=["$log","$rootScope","$http","flash"],angular.module("app.services").factory("FlameGraphService",e)}(),/**!
 *
 *  Copyright 2015 Netflix, Inc.
 *
 *     Licensed under the Apache License, Version 2.0 (the "License");
 *     you may not use this file except in compliance with the License.
 *     You may obtain a copy of the License at
 *
 *         http://www.apache.org/licenses/LICENSE-2.0
 *
 *     Unless required by applicable law or agreed to in writing, software
 *     distributed under the License is distributed on an "AS IS" BASIS,
 *     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *     See the License for the specific language governing permissions and
 *     limitations under the License.
 *
 */
function(){"use strict";function e(e,t,a,i,r,n,o,s,c){function l(){b&&(a.cancel(b),i.info("Interval canceled."))}function d(e){e?k=0:k+=1,k>5&&(l(b),k=0,s.to("alert-dashboard-error").error="Consistently failed fetching metrics from host (>5). Aborting loop. Please make sure PCP is running correctly.")}function p(){o.updateMetrics(d),o.updateDerivedMetrics()}function u(){l(b),e.properties.host&&(e.properties.context&&e.properties.context>0?b=a(p,1e3*e.properties.interval):s.to("alert-dashboard-error").error="Invalid context. Please update host to resume operation.",i.info("Interval updated."))}function m(t){e.properties.hostname=t.values[0].instances[0].value,i.info("Hostname updated: "+e.properties.hostname)}function g(){e.properties.hostname="Hostname not available.",i.error("Error fetching hostname.")}function h(t){e.flags.contextAvailable=!0,e.properties.context=t,u()}function f(){e.flags.contextAvailable=!1,i.error("Error fetching context.")}function v(t){i.info("Context updated.");var a=e.properties.hostspec,r=null;t&&""!==t&&(e.flags.contextUpdating=!0,e.flags.contextAvailable=!1,r=t.match("(.*):([0-9]*)"),null!==r?(e.properties.host=r[1],e.properties.port=r[2]):e.properties.host=t,n.getHostspecContext(a,600).then(function(t){e.flags.contextUpdating=!1,h(t),n.getMetrics(t,["pmcd.hostname"]).then(function(e){m(e)},function(){g()})},function(){s.to("alert-dashboard-error").error="Failed fetching context from host. Try updating the hostname.",e.flags.contextUpdating=!1,f()}))}function y(t){i.info("Host updated."),r.search("host",t),r.search("hostspec",e.properties.hostspec),e.properties.context=-1,e.properties.hostname=null,e.properties.port=c.port,o.clearMetricList(),o.clearDerivedMetricList(),v(t)}function w(){i.log("Window updated.")}function M(){e.properties?(e.properties.interval||(e.properties.interval=c.interval),e.properties.window||(e.properties.window=c.window),e.properties.protocol||(e.properties.protocol=c.protocol),e.properties.host||(e.properties.host=""),e.properties.hostspec||(e.properties.hostspec=c.hostspec),e.properties.port||(e.properties.port=c.port),!e.properties.context||e.properties.context<0?v():u()):e.properties={protocol:c.protocol,host:"",hostspec:c.hostspec,port:c.port,context:-1,hostname:null,window:c.window,interval:c.interval},e.flags={contextAvailable:!1,contextUpdating:!1}}var b,k=0;return{updateContext:v,cancelInterval:l,updateInterval:u,updateHost:y,updateWindow:w,initializeProperties:M}}e.$inject=["$rootScope","$http","$interval","$log","$location","PMAPIService","MetricListService","flash","vectorConfig"],angular.module("app.services").factory("DashboardService",e)}(),/**!
 *
 *  Copyright 2015 Netflix, Inc.
 *
 *     Licensed under the Apache License, Version 2.0 (the "License");
 *     you may not use this file except in compliance with the License.
 *     You may obtain a copy of the License at
 *
 *         http://www.apache.org/licenses/LICENSE-2.0
 *
 *     Unless required by applicable law or agreed to in writing, software
 *     distributed under the License is distributed on an "AS IS" BASIS,
 *     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *     See the License for the specific language governing permissions and
 *     limitations under the License.
 *
 */
function(){"use strict";function e(){function e(){return function(e){return d3.time.format("%X")(new Date(e))}}function t(){return function(e){return d3.format(".02f")(e)}}function a(){return function(e){return d3.format("f")(e)}}function i(){return function(e){return d3.format("%")(e)}}function r(){return function(e){return e.x}}function n(){return function(e){return e.y}}function o(){return"chart_"+Math.floor(65536*(1+Math.random())).toString(16).substring(1)}return{xAxisTickFormat:e,yAxisTickFormat:t,yAxisIntegerTickFormat:a,yAxisPercentageTickFormat:i,xFunction:r,yFunction:n,getId:o}}angular.module("app.services").factory("D3Service",e)}(),/**!
 *
 *  Copyright 2015 Netflix, Inc.
 *
 *     Licensed under the Apache License, Version 2.0 (the "License");
 *     you may not use this file except in compliance with the License.
 *     You may obtain a copy of the License at
 *
 *         http://www.apache.org/licenses/LICENSE-2.0
 *
 *     Unless required by applicable law or agreed to in writing, software
 *     distributed under the License is distributed on an "AS IS" BASIS,
 *     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *     See the License for the specific language governing permissions and
 *     limitations under the License.
 *
 */
function(){"use strict";function e(e,t,a){var i=function(){return this};return i.prototype=Object.create(e.prototype),i.prototype.init=function(){e.prototype.init.call(this),this.name=this.dataModelOptions?this.dataModelOptions.name:"metric_"+a.getGuid(),this.metric=t.getOrCreateMetric(this.name),this.updateScope(this.metric.data)},i.prototype.destroy=function(){t.destroyMetric(this.name),e.prototype.destroy.call(this)},i}e.$inject=["WidgetDataModel","MetricListService","VectorService"],angular.module("app.datamodels").factory("MetricDataModel",e)}(),/**!
 *
 *  Copyright 2015 Netflix, Inc.
 *
 *     Licensed under the Apache License, Version 2.0 (the "License");
 *     you may not use this file except in compliance with the License.
 *     You may obtain a copy of the License at
 *
 *         http://www.apache.org/licenses/LICENSE-2.0
 *
 *     Unless required by applicable law or agreed to in writing, software
 *     distributed under the License is distributed on an "AS IS" BASIS,
 *     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *     See the License for the specific language governing permissions and
 *     limitations under the License.
 *
 */
function(){"use strict";function e(e,t,a){var i=function(){return this};return i.prototype=Object.create(e.prototype),i.prototype.init=function(){e.prototype.init.call(this),this.name=this.dataModelOptions?this.dataModelOptions.name:"metric_"+a.getGuid();var i,r=t.getOrCreateCumulativeMetric("kernel.percpu.cpu.sys"),n=t.getOrCreateCumulativeMetric("kernel.percpu.cpu.user");i=function(){var e,t,a,i=[];return angular.forEach(r.data,function(r){r.values.length>0&&(e=_.find(n.data,function(e){return e.key===r.key}),angular.isDefined(e)&&(t=r.values[r.values.length-1],a=e.values[e.values.length-1],t.x===a.x&&i.push({timestamp:t.x,key:r.key,value:(t.y+a.y)/1e3})))}),i},this.metric=t.getOrCreateDerivedMetric(this.name,i),this.updateScope(this.metric.data)},i.prototype.destroy=function(){t.destroyDerivedMetric(this.name),t.destroyMetric("kernel.percpu.cpu.sys"),t.destroyMetric("kernel.percpu.cpu.user"),e.prototype.destroy.call(this)},i}e.$inject=["WidgetDataModel","MetricListService","VectorService"],angular.module("app.datamodels").factory("PerCpuUtilizationMetricDataModel",e)}(),/**!
 *
 *  Copyright 2015 Netflix, Inc.
 *
 *     Licensed under the Apache License, Version 2.0 (the "License");
 *     you may not use this file except in compliance with the License.
 *     You may obtain a copy of the License at
 *
 *         http://www.apache.org/licenses/LICENSE-2.0
 *
 *     Unless required by applicable law or agreed to in writing, software
 *     distributed under the License is distributed on an "AS IS" BASIS,
 *     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *     See the License for the specific language governing permissions and
 *     limitations under the License.
 *
 */
function(){"use strict";function e(e,t,a){var i=function(){return this};return i.prototype=Object.create(e.prototype),i.prototype.init=function(){e.prototype.init.call(this),this.name=this.dataModelOptions?this.dataModelOptions.name:"metric_"+a.getGuid();var i,r=t.getOrCreateCumulativeMetric("network.interface.in.bytes"),n=t.getOrCreateCumulativeMetric("network.interface.out.bytes");i=function(){var e,t=[];return angular.forEach(r.data,function(a){a.values.length>0&&(e=a.values[a.values.length-1],t.push({timestamp:e.x,key:a.key+" in",value:e.y/1024}))}),angular.forEach(n.data,function(a){a.values.length>0&&(e=a.values[a.values.length-1],t.push({timestamp:e.x,key:a.key+" out",value:e.y/1024}))}),t},this.metric=t.getOrCreateDerivedMetric(this.name,i),this.updateScope(this.metric.data)},i.prototype.destroy=function(){t.destroyDerivedMetric(this.name),t.destroyMetric("network.interface.in.bytes"),t.destroyMetric("network.interface.out.bytes"),e.prototype.destroy.call(this)},i}e.$inject=["WidgetDataModel","MetricListService","VectorService"],angular.module("app.datamodels").factory("NetworkBytesMetricDataModel",e)}(),/**!
 *
 *  Copyright 2015 Netflix, Inc.
 *
 *     Licensed under the Apache License, Version 2.0 (the "License");
 *     you may not use this file except in compliance with the License.
 *     You may obtain a copy of the License at
 *
 *         http://www.apache.org/licenses/LICENSE-2.0
 *
 *     Unless required by applicable law or agreed to in writing, software
 *     distributed under the License is distributed on an "AS IS" BASIS,
 *     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *     See the License for the specific language governing permissions and
 *     limitations under the License.
 *
 */
function(){"use strict";function e(e,t,a){var i=function(){return this};return i.prototype=Object.create(e.prototype),i.prototype.init=function(){e.prototype.init.call(this),this.name=this.dataModelOptions?this.dataModelOptions.name:"metric_"+a.getGuid(),this.metricDefinitions=this.dataModelOptions.metricDefinitions;var i,r={};angular.forEach(this.metricDefinitions,function(e,a){r[a]=t.getOrCreateMetric(e)}),i=function(){var e,t=[];return angular.forEach(r,function(a,i){angular.forEach(a.data,function(a){a.values.length>0&&(e=a.values[a.values.length-1],t.push({timestamp:e.x,key:i.replace("{key}",a.key),value:e.y}))})}),t},this.metric=t.getOrCreateDerivedMetric(this.name,i),this.updateScope(this.metric.data)},i.prototype.destroy=function(){t.destroyDerivedMetric(this.name),angular.forEach(this.metricDefinitions,function(e){t.destroyMetric(e)}),e.prototype.destroy.call(this)},i}e.$inject=["WidgetDataModel","MetricListService","VectorService"],angular.module("app.datamodels").factory("MultipleMetricDataModel",e)}(),/**!
 *
 *  Copyright 2015 Netflix, Inc.
 *
 *     Licensed under the Apache License, Version 2.0 (the "License");
 *     you may not use this file except in compliance with the License.
 *     You may obtain a copy of the License at
 *
 *         http://www.apache.org/licenses/LICENSE-2.0
 *
 *     Unless required by applicable law or agreed to in writing, software
 *     distributed under the License is distributed on an "AS IS" BASIS,
 *     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *     See the License for the specific language governing permissions and
 *     limitations under the License.
 *
 */
function(){"use strict";function e(e,t,a){var i=function(){return this};return i.prototype=Object.create(e.prototype),i.prototype.init=function(){e.prototype.init.call(this),this.name=this.dataModelOptions?this.dataModelOptions.name:"metric_"+a.getGuid(),this.metricDefinitions=this.dataModelOptions.metricDefinitions;var i,r={};angular.forEach(this.metricDefinitions,function(e,a){r[a]=t.getOrCreateCumulativeMetric(e)}),i=function(){var e,t=[];return angular.forEach(r,function(a,i){angular.forEach(a.data,function(a){a.values.length>0&&(e=a.values[a.values.length-1],t.push({timestamp:e.x,key:i.replace("{key}",a.key),value:e.y}))})}),t},this.metric=t.getOrCreateDerivedMetric(this.name,i),this.updateScope(this.metric.data)},i.prototype.destroy=function(){t.destroyDerivedMetric(this.name),angular.forEach(this.metricDefinitions,function(e){t.destroyMetric(e)}),e.prototype.destroy.call(this)},i}e.$inject=["WidgetDataModel","MetricListService","VectorService"],angular.module("app.datamodels").factory("MultipleCumulativeMetricDataModel",e)}(),/**!
 *
 *  Copyright 2015 Netflix, Inc.
 *
 *     Licensed under the Apache License, Version 2.0 (the "License");
 *     you may not use this file except in compliance with the License.
 *     You may obtain a copy of the License at
 *
 *         http://www.apache.org/licenses/LICENSE-2.0
 *
 *     Unless required by applicable law or agreed to in writing, software
 *     distributed under the License is distributed on an "AS IS" BASIS,
 *     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *     See the License for the specific language governing permissions and
 *     limitations under the License.
 *
 */
function(){"use strict";function e(e,t,a){var i=function(){return this};return i.prototype=Object.create(e.prototype),i.prototype.init=function(){e.prototype.init.call(this),this.name=this.dataModelOptions?this.dataModelOptions.name:"metric_"+a.getGuid();var i,r=function(e){return e/1024},n=t.getOrCreateConvertedMetric("mem.util.cached",r),o=t.getOrCreateConvertedMetric("mem.util.used",r),s=t.getOrCreateConvertedMetric("mem.util.free",r),c=t.getOrCreateConvertedMetric("mem.util.bufmem",r);i=function(){var e,t,a,i,r=[];return e=function(){if(o.data.length>0){var e=o.data[o.data.length-1];if(e.values.length>0)return e.values[e.values.length-1]}}(),t=function(){if(n.data.length>0){var e=n.data[n.data.length-1];if(e.values.length>0)return e.values[e.values.length-1]}}(),a=function(){if(s.data.length>0){var e=s.data[s.data.length-1];if(e.values.length>0)return e.values[e.values.length-1]}}(),i=function(){if(c.data.length>0){var e=c.data[c.data.length-1];if(e.values.length>0)return e.values[e.values.length-1]}}(),angular.isDefined(e)&&angular.isDefined(t)&&angular.isDefined(i)&&r.push({timestamp:e.x,key:"application",value:e.y-t.y-i.y}),angular.isDefined(t)&&angular.isDefined(i)&&r.push({timestamp:e.x,key:"free (cache)",value:t.y+i.y}),angular.isDefined(a)&&r.push({timestamp:e.x,key:"free (unused)",value:a.y}),r},this.metric=t.getOrCreateDerivedMetric(this.name,i),this.updateScope(this.metric.data)},i.prototype.destroy=function(){t.destroyDerivedMetric(this.name),t.destroyMetric("mem.util.cached"),t.destroyMetric("mem.util.used"),t.destroyMetric("mem.util.free"),t.destroyMetric("mem.util.bufmem"),e.prototype.destroy.call(this)},i}e.$inject=["WidgetDataModel","MetricListService","VectorService"],angular.module("app.datamodels").factory("MemoryUtilizationMetricDataModel",e)}(),/**!
 *
 *  Copyright 2015 Netflix, Inc.
 *
 *     Licensed under the Apache License, Version 2.0 (the "License");
 *     you may not use this file except in compliance with the License.
 *     You may obtain a copy of the License at
 *
 *         http://www.apache.org/licenses/LICENSE-2.0
 *
 *     Unless required by applicable law or agreed to in writing, software
 *     distributed under the License is distributed on an "AS IS" BASIS,
 *     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *     See the License for the specific language governing permissions and
 *     limitations under the License.
 *
 */
function(){"use strict";function e(e,t){var a=function(){return this};return a.prototype=Object.create(e.prototype),a.prototype.init=function(){e.prototype.init.call(this),this.metric=t.getOrCreateMetric("kernel.uname.release")},a.prototype.destroy=function(){t.destroyMetric("kernel.uname.release"),e.prototype.destroy.call(this)},a}e.$inject=["WidgetDataModel","MetricListService"],angular.module("app.datamodels").factory("DummyMetricDataModel",e)}(),/**!
 *
 *  Copyright 2015 Netflix, Inc.
 *
 *     Licensed under the Apache License, Version 2.0 (the "License");
 *     you may not use this file except in compliance with the License.
 *     You may obtain a copy of the License at
 *
 *         http://www.apache.org/licenses/LICENSE-2.0
 *
 *     Unless required by applicable law or agreed to in writing, software
 *     distributed under the License is distributed on an "AS IS" BASIS,
 *     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *     See the License for the specific language governing permissions and
 *     limitations under the License.
 *
 */
function(){"use strict";function e(e,t,a){var i=function(){return this};return i.prototype=Object.create(e.prototype),i.prototype.init=function(){e.prototype.init.call(this),this.name=this.dataModelOptions?this.dataModelOptions.name:"metric_"+a.getGuid();var i,r=t.getOrCreateCumulativeMetric("disk.dev.read_rawactive"),n=t.getOrCreateCumulativeMetric("disk.dev.write_rawactive"),o=t.getOrCreateCumulativeMetric("disk.dev.read"),s=t.getOrCreateCumulativeMetric("disk.dev.write");i=function(){function e(e,r,n,o){e.data.length>0&&angular.forEach(e.data,function(e){t=_.find(r.data,function(t){return t.key===e.key}),angular.isDefined(t)&&e.values.length>0&&t.values.length>0&&(a=e.values[e.values.length-1],i=t.values[e.values.length-1],c=a.y>0?i.y/a.y:0,o.push({timestamp:a.x,key:e.key+n,value:c}))})}var t,a,i,c,l=[];return e(o,r," read latency",l),e(s,n," write latency",l),l},this.metric=t.getOrCreateDerivedMetric(this.name,i),this.updateScope(this.metric.data)},i.prototype.destroy=function(){t.destroyDerivedMetric(this.name),t.destroyMetric("disk.dev.read_rawactive"),t.destroyMetric("disk.dev.write_rawactive"),t.destroyMetric("disk.dev.read"),t.destroyMetric("disk.dev.write"),e.prototype.destroy.call(this)},i}e.$inject=["WidgetDataModel","MetricListService","VectorService"],angular.module("app.datamodels").factory("DiskLatencyMetricDataModel",e)}(),/**!
 *
 *  Copyright 2015 Netflix, Inc.
 *
 *     Licensed under the Apache License, Version 2.0 (the "License");
 *     you may not use this file except in compliance with the License.
 *     You may obtain a copy of the License at
 *
 *         http://www.apache.org/licenses/LICENSE-2.0
 *
 *     Unless required by applicable law or agreed to in writing, software
 *     distributed under the License is distributed on an "AS IS" BASIS,
 *     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *     See the License for the specific language governing permissions and
 *     limitations under the License.
 *
 */
function(){"use strict";function e(e,t,a){var i=function(){return this};return i.prototype=Object.create(e.prototype),i.prototype.init=function(){e.prototype.init.call(this),this.name=this.dataModelOptions?this.dataModelOptions.name:"metric_"+a.getGuid();var i,r=t.getOrCreateCumulativeMetric(this.name);i=function(){var e,t=[];return angular.forEach(r.data,function(a){a.values.length>0&&(e=a.values[a.values.length-1],t.push({timestamp:e.x,key:a.key,value:e.y/1e3}))}),t},this.metric=t.getOrCreateDerivedMetric(this.name,i),this.updateScope(this.metric.data)},i.prototype.destroy=function(){t.destroyDerivedMetric(this.name),t.destroyMetric(this.name),e.prototype.destroy.call(this)},i}e.$inject=["WidgetDataModel","MetricListService","VectorService"],angular.module("app.datamodels").factory("CumulativeUtilizationMetricDataModel",e)}(),/**!
 *
 *  Copyright 2015 Netflix, Inc.
 *
 *     Licensed under the Apache License, Version 2.0 (the "License");
 *     you may not use this file except in compliance with the License.
 *     You may obtain a copy of the License at
 *
 *         http://www.apache.org/licenses/LICENSE-2.0
 *
 *     Unless required by applicable law or agreed to in writing, software
 *     distributed under the License is distributed on an "AS IS" BASIS,
 *     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *     See the License for the specific language governing permissions and
 *     limitations under the License.
 *
 */
function(){"use strict";function e(e,t,a){var i=function(){return this};return i.prototype=Object.create(e.prototype),i.prototype.init=function(){e.prototype.init.call(this),this.name=this.dataModelOptions?this.dataModelOptions.name:"metric_"+a.getGuid(),this.metric=t.getOrCreateCumulativeMetric(this.name),this.updateScope(this.metric.data)},i.prototype.destroy=function(){t.destroyMetric(this.name),e.prototype.destroy.call(this)},i}e.$inject=["WidgetDataModel","MetricListService","VectorService"],angular.module("app.datamodels").factory("CumulativeMetricDataModel",e)}(),/**!
 *
 *  Copyright 2015 Netflix, Inc.
 *
 *     Licensed under the Apache License, Version 2.0 (the "License");
 *     you may not use this file except in compliance with the License.
 *     You may obtain a copy of the License at
 *
 *         http://www.apache.org/licenses/LICENSE-2.0
 *
 *     Unless required by applicable law or agreed to in writing, software
 *     distributed under the License is distributed on an "AS IS" BASIS,
 *     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *     See the License for the specific language governing permissions and
 *     limitations under the License.
 *
 */
function(){"use strict";function e(e,t,a){var i=function(){return this};return i.prototype=Object.create(e.prototype),i.prototype.init=function(){e.prototype.init.call(this),this.name=this.dataModelOptions?this.dataModelOptions.name:"metric_"+a.getGuid();var i,r=t.getOrCreateCumulativeMetric("kernel.all.cpu.sys"),n=t.getOrCreateCumulativeMetric("kernel.all.cpu.user"),o=t.getOrCreateMetric("hinv.ncpu");i=function(){var e,t,a=[];return o.data.length>0&&(e=o.data[o.data.length-1],e.values.length>0&&(t=e.values[e.values.length-1].y,angular.forEach(r.data,function(e){if(e.values.length>0){var i=e.values[e.values.length-1];a.push({timestamp:i.x,key:"sys",value:i.y/(1e3*t)})}}),angular.forEach(n.data,function(e){if(e.values.length>0){var i=e.values[e.values.length-1];a.push({timestamp:i.x,key:"user",value:i.y/(1e3*t)})}}))),a},this.metric=t.getOrCreateDerivedMetric(this.name,i),this.updateScope(this.metric.data)},i.prototype.destroy=function(){t.destroyDerivedMetric(this.name),t.destroyMetric("kernel.all.cpu.sys"),t.destroyMetric("kernel.all.cpu.user"),t.destroyMetric("hinv.ncpu"),e.prototype.destroy.call(this)},i}e.$inject=["WidgetDataModel","MetricListService","VectorService"],angular.module("app.datamodels").factory("CpuUtilizationMetricDataModel",e)}(),/**!
 *
 *  Copyright 2015 Netflix, Inc.
 *
 *     Licensed under the Apache License, Version 2.0 (the "License");
 *     you may not use this file except in compliance with the License.
 *     You may obtain a copy of the License at
 *
 *         http://www.apache.org/licenses/LICENSE-2.0
 *
 *     Unless required by applicable law or agreed to in writing, software
 *     distributed under the License is distributed on an "AS IS" BASIS,
 *     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *     See the License for the specific language governing permissions and
 *     limitations under the License.
 *
 */
function(){"use strict";function e(e){var t=function(e){this.name=e||null,this.data=[],this.subscribers=1};return t.prototype.toString=function(){return this.name},t.prototype.pushValue=function(t,a,i,r){var n,o,s=this;n=_.find(s.data,function(e){return e.iid===a}),angular.isDefined(n)&&null!==n?(n.values.push({x:t,y:r}),o=n.values.length-60*e.properties.window/e.properties.interval,o>0&&n.values.splice(0,o)):(n={key:angular.isDefined(i)?i:this.name,iid:a,values:[{x:t,y:r},{x:t+1,y:r}]},s.data.push(n))},t.prototype.clearData=function(){this.data.length=0},t}e.$inject=["$rootScope"],angular.module("app.metrics").factory("SimpleMetric",e)}(),/**!
 *
 *  Copyright 2015 Netflix, Inc.
 *
 *     Licensed under the Apache License, Version 2.0 (the "License");
 *     you may not use this file except in compliance with the License.
 *     You may obtain a copy of the License at
 *
 *         http://www.apache.org/licenses/LICENSE-2.0
 *
 *     Unless required by applicable law or agreed to in writing, software
 *     distributed under the License is distributed on an "AS IS" BASIS,
 *     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *     See the License for the specific language governing permissions and
 *     limitations under the License.
 *
 */
function(){"use strict";function e(e){var t=function(e,t){this.name=e,this.data=[],this.subscribers=1,this.derivedFunction=t};return t.prototype.updateValues=function(){var t,a=this;t=a.derivedFunction(),t.length!==a.data.length&&(a.data.length=0),angular.forEach(t,function(t){var i,r=_.find(a.data,function(e){return e.key===t.key});angular.isUndefined(r)?(r={key:t.key,values:[{x:t.timestamp,y:t.value},{x:t.timestamp+1,y:t.value}]},a.data.push(r)):(r.values.push({x:t.timestamp,y:t.value}),i=r.values.length-60*e.properties.window/e.properties.interval,i>0&&r.values.splice(0,i))})},t.prototype.clearData=function(){this.data.length=0},t}e.$inject=["$rootScope"],angular.module("app.metrics").factory("DerivedMetric",e)}(),/**!
 *
 *  Copyright 2015 Netflix, Inc.
 *
 *     Licensed under the Apache License, Version 2.0 (the "License");
 *     you may not use this file except in compliance with the License.
 *     You may obtain a copy of the License at
 *
 *         http://www.apache.org/licenses/LICENSE-2.0
 *
 *     Unless required by applicable law or agreed to in writing, software
 *     distributed under the License is distributed on an "AS IS" BASIS,
 *     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *     See the License for the specific language governing permissions and
 *     limitations under the License.
 *
 */
function(){"use strict";function e(e,t,a){var i=function(e,t){this.base=i,this.base(e),this.conversionFunction=t};return i.prototype=new a,i.prototype.pushValue=function(t,a,i,r){var n,o,s,c,l=this;n=_.find(l.data,function(e){return e.iid===a}),angular.isUndefined(n)?(n={key:angular.isDefined(i)?i:this.name,iid:a,values:[],previousValue:r,previousTimestamp:t},l.data.push(n)):(s=(r-n.previousValue)/(t-n.previousTimestamp),c=l.conversionFunction(s),n.values.push({x:t,y:c}),n.previousValue=r,n.previousTimestamp=t,o=n.values.length-60*e.properties.window/e.properties.interval,o>0&&n.values.splice(0,o))},i}e.$inject=["$rootScope","$log","SimpleMetric"],angular.module("app.metrics").factory("CumulativeConvertedMetric",e)}(),/**!
 *
 *  Copyright 2015 Netflix, Inc.
 *
 *     Licensed under the Apache License, Version 2.0 (the "License");
 *     you may not use this file except in compliance with the License.
 *     You may obtain a copy of the License at
 *
 *         http://www.apache.org/licenses/LICENSE-2.0
 *
 *     Unless required by applicable law or agreed to in writing, software
 *     distributed under the License is distributed on an "AS IS" BASIS,
 *     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *     See the License for the specific language governing permissions and
 *     limitations under the License.
 *
 */
function(){"use strict";function e(e,t,a){var i=function(e){this.base=a,this.base(e)};return i.prototype=new a,i.prototype.pushValue=function(t,a,i,r){var n,o,s,c=this;n=_.find(c.data,function(e){return e.iid===a}),angular.isUndefined(n)?(n={key:angular.isDefined(i)?i:this.name,iid:a,values:[],previousValue:r,previousTimestamp:t},c.data.push(n)):(s=(r-n.previousValue)/((t-n.previousTimestamp)/1e3),n.values.length<1?n.values.push({x:t,y:s},{x:t+1,y:s}):n.values.push({x:t,y:s}),n.previousValue=r,n.previousTimestamp=t,o=n.values.length-60*e.properties.window/e.properties.interval,o>0&&n.values.splice(0,o))},i}e.$inject=["$rootScope","$log","SimpleMetric"],angular.module("app.metrics").factory("CumulativeMetric",e)}(),/**!
 *
 *  Copyright 2015 Netflix, Inc.
 *
 *     Licensed under the Apache License, Version 2.0 (the "License");
 *     you may not use this file except in compliance with the License.
 *     You may obtain a copy of the License at
 *
 *         http://www.apache.org/licenses/LICENSE-2.0
 *
 *     Unless required by applicable law or agreed to in writing, software
 *     distributed under the License is distributed on an "AS IS" BASIS,
 *     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *     See the License for the specific language governing permissions and
 *     limitations under the License.
 *
 */
function(){"use strict";function e(e,t,a){var i=function(e,t){this.base=a,this.base(e),this.conversionFunction=t};return i.prototype=new a,i.prototype.pushValue=function(t,a,i,r){var n,o,s,c=this;s=c.conversionFunction(r),n=_.find(c.data,function(e){return e.iid===a}),angular.isDefined(n)&&null!==n?(n.values.push({x:t,y:s}),o=n.values.length-60*e.properties.window/e.properties.interval,o>0&&n.values.splice(0,o)):(n={key:angular.isDefined(i)?i:this.name,iid:a,values:[{x:t,y:s},{x:t+1,y:s}]},c.data.push(n))},i}e.$inject=["$rootScope","$log","SimpleMetric"],angular.module("app.metrics").factory("ConvertedMetric",e)}(),/**!
 *
 *  Copyright 2015 Netflix, Inc.
 *
 *     Licensed under the Apache License, Version 2.0 (the "License");
 *     you may not use this file except in compliance with the License.
 *     You may obtain a copy of the License at
 *
 *         http://www.apache.org/licenses/LICENSE-2.0
 *
 *     Unless required by applicable law or agreed to in writing, software
 *     distributed under the License is distributed on an "AS IS" BASIS,
 *     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *     See the License for the specific language governing permissions and
 *     limitations under the License.
 *
 */
function(){"use strict";function e(e,t,a,i,r,n,o,s){function c(){e[0].hidden||e[0].webkitHidden||e[0].mozHidden||e[0].msHidden?s.cancelInterval():s.updateInterval()}function l(){s.initializeProperties(),r.protocol&&(t.properties.protocol=r.protocol,a.info("Protocol: "+r.protocol)),r.host&&(d.inputHost=r.host,a.info("Host: "+r.host),r.hostspec&&(t.properties.hostspec=r.hostspec,a.info("Hostspec: "+r.hostspec)),s.updateHost(d.inputHost)),e[0].addEventListener("visibilitychange",c,!1),e[0].addEventListener("webkitvisibilitychange",c,!1),e[0].addEventListener("msvisibilitychange",c,!1),e[0].addEventListener("mozvisibilitychange",c,!1),a.info("Dashboard controller initialized with "+p+" view.")}var d=this,p=i.current.$$route.originalPath;d.dashboardOptions={hideToolbar:!0,widgetButtons:!1,hideWidgetName:!0,hideWidgetSettings:!0,widgetDefinitions:n,defaultWidgets:o},d.updateInterval=s.updateInterval,d.updateHost=function(){s.updateHost(d.inputHost)},d.updateWindow=s.updateWindow,d.isHostnameExpanded=!1,d.inputHost="",l()}e.$inject=["$document","$rootScope","$log","$route","$routeParams","widgetDefinitions","widgets","DashboardService"],angular.module("app.controllers",[]).controller("DashboardController",e)}(),/**!
 *
 *  Copyright 2015 Netflix, Inc.
 *
 *     Licensed under the Apache License, Version 2.0 (the "License");
 *     you may not use this file except in compliance with the License.
 *     You may obtain a copy of the License at
 *
 *         http://www.apache.org/licenses/LICENSE-2.0
 *
 *     Unless required by applicable law or agreed to in writing, software
 *     distributed under the License is distributed on an "AS IS" BASIS,
 *     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *     See the License for the specific language governing permissions and
 *     limitations under the License.
 *
 */
function(){"use strict";function e(e,t,a){function i(t){t.id=a.getId(),t.flags=e.flags;var i;nv.addGraph(function(){var e=250;return i=nv.models.lineChart().options({duration:0,useInteractiveGuideline:!0,interactive:!1,showLegend:!0,showXAxis:!0,showYAxis:!0}),i.margin({left:35,right:35}),i.height(e),t.forcey&&i.forceY([0,t.forcey]),i.x(a.xFunction()),i.y(a.yFunction()),i.xAxis.tickFormat(a.xAxisTickFormat()),i.yAxis.tickFormat(t.percentage?a.yAxisPercentageTickFormat():t.integer?a.yAxisIntegerTickFormat():a.yAxisTickFormat()),nv.utils.windowResize(i.update),d3.select("#"+t.id+" svg").datum(t.data).style({height:e}).transition().duration(0).call(i),i}),t.$on("updateMetrics",function(){i.update()})}return{restrict:"A",templateUrl:"app/charts/nvd3-chart.html",scope:{data:"=",percentage:"=",integer:"=",forcey:"="},link:i}}e.$inject=["$rootScope","$log","D3Service"],angular.module("app.charts").directive("lineTimeSeries",e)}(),/**!
 *
 *  Copyright 2015 Netflix, Inc.
 *
 *     Licensed under the Apache License, Version 2.0 (the "License");
 *     you may not use this file except in compliance with the License.
 *     You may obtain a copy of the License at
 *
 *         http://www.apache.org/licenses/LICENSE-2.0
 *
 *     Unless required by applicable law or agreed to in writing, software
 *     distributed under the License is distributed on an "AS IS" BASIS,
 *     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *     See the License for the specific language governing permissions and
 *     limitations under the License.
 *
 */
function(){"use strict";function e(e,t,a,i){function r(r){r.host=e.properties.host,r.port=e.properties.port,r.context=e.properties.context,r.ready=!1,r.processing=!1,r.id=i.getGuid(),r.generateHeatMap=function(){a.generate(),r.ready=!0,r.processing=!0,t(function(){r.processing=!1},15e4)}}return{restrict:"A",templateUrl:"app/charts/disk-latency-heatmap.html",link:r}}e.$inject=["$rootScope","$timeout","HeatMapService","VectorService"],angular.module("app.charts").directive("diskLatencyHeatMap",e)}(),/**!
 *
 *  Copyright 2015 Netflix, Inc.
 *
 *     Licensed under the Apache License, Version 2.0 (the "License");
 *     you may not use this file except in compliance with the License.
 *     You may obtain a copy of the License at
 *
 *         http://www.apache.org/licenses/LICENSE-2.0
 *
 *     Unless required by applicable law or agreed to in writing, software
 *     distributed under the License is distributed on an "AS IS" BASIS,
 *     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *     See the License for the specific language governing permissions and
 *     limitations under the License.
 *
 */
function(){"use strict";function e(e,t,a,i){function r(r){r.host=e.properties.host,r.port=e.properties.port,r.context=e.properties.context,r.ready=!1,r.processing=!1,r.id=i.getGuid(),r.generateFlameGraph=function(){a.generate(),r.ready=!0,r.processing=!0,t(function(){r.processing=!1},65e3)}}return{restrict:"A",templateUrl:"app/charts/cpu-flame-graph.html",link:r}}e.$inject=["$rootScope","$timeout","FlameGraphService","VectorService"],angular.module("app.charts").directive("cpuFlameGraph",e)}(),/**!
 *
 *  Copyright 2015 Netflix, Inc.
 *
 *     Licensed under the Apache License, Version 2.0 (the "License");
 *     you may not use this file except in compliance with the License.
 *     You may obtain a copy of the License at
 *
 *         http://www.apache.org/licenses/LICENSE-2.0
 *
 *     Unless required by applicable law or agreed to in writing, software
 *     distributed under the License is distributed on an "AS IS" BASIS,
 *     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *     See the License for the specific language governing permissions and
 *     limitations under the License.
 *
 */
function(){"use strict";function e(e,t,a){function i(t){t.id=a.getId(),t.flags=e.flags,t.legend=!0;var i;nv.addGraph(function(){var e=a.yAxisTickFormat(),r=250;return i=nv.models.stackedAreaChart().options({duration:0,useInteractiveGuideline:!0,interactive:!1,showLegend:!0,showXAxis:!0,showYAxis:!0,showControls:!1}),i.margin({left:35,right:35}),i.height(r),t.forcey&&i.yDomain([0,t.forcey]),i.x(a.xFunction()),i.y(a.yFunction()),i.xAxis.tickFormat(a.xAxisTickFormat()),t.percentage?(e=a.yAxisPercentageTickFormat(),i.yAxis.tickFormat()):t.integer&&(e=a.yAxisIntegerTickFormat(),i.yAxis.tickFormat()),i.yAxis.tickFormat(e),i.interactiveLayer.tooltip.contentGenerator(function(t){var a=t.value,i='<thead><tr><td colspan="3"><strong class="x-value">'+a+"</strong></td></tr></thead>",r="<tbody>",n=t.series;return n.forEach(function(t){r=r+'<tr><td class="legend-color-guide"><div style="background-color: '+t.color+';"></div></td><td class="key">'+t.key+'</td><td class="value">'+e(t.value)+"</td></tr>"}),r+="</tbody>","<table>"+i+r+"</table>"}),nv.utils.windowResize(i.update),d3.select("#"+t.id+" svg").datum(t.data).style({height:r}).transition().duration(0).call(i),i}),t.$on("updateMetrics",function(){i.update()})}return{restrict:"A",templateUrl:"app/charts/nvd3-chart.html",scope:{data:"=",percentage:"=",integer:"=",forcey:"="},link:i}}e.$inject=["$rootScope","$log","D3Service"],angular.module("app.charts").directive("areaStackedTimeSeries",e)}(),/**!
 *
 *  Copyright 2015 Netflix, Inc.
 *
 *     Licensed under the Apache License, Version 2.0 (the "License");
 *     you may not use this file except in compliance with the License.
 *     You may obtain a copy of the License at
 *
 *         http://www.apache.org/licenses/LICENSE-2.0
 *
 *     Unless required by applicable law or agreed to in writing, software
 *     distributed under the License is distributed on an "AS IS" BASIS,
 *     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *     See the License for the specific language governing permissions and
 *     limitations under the License.
 *
 */
function(){"use strict";function e(e,t,a,i,r,n,o,s,c,l,d,p){var u=[{name:"kernel.all.load",title:"Load Average",directive:"line-time-series",dataAttrName:"data",dataModelType:e,dataModelOptions:{name:"kernel.all.load"},size:{width:"25%",height:"250px"},enableVerticalResize:!1,group:"CPU"},{name:"kernel.all.runnable",title:"Runnable",directive:"line-time-series",dataAttrName:"data",dataModelType:e,dataModelOptions:{name:"kernel.all.runnable"},size:{width:"25%",height:"250px"},enableVerticalResize:!1,group:"CPU",attrs:{forcey:4,integer:!0}},{name:"kernel.all.cpu.sys",title:"CPU Utilization (System)",directive:"line-time-series",dataAttrName:"data",dataModelType:d,dataModelOptions:{name:"kernel.all.cpu.sys"},size:{width:"25%",height:"250px"},enableVerticalResize:!1,group:"CPU",attrs:{forcey:1,percentage:!0,integer:!1}},{name:"kernel.all.cpu.user",title:"CPU Utilization (User)",directive:"line-time-series",dataAttrName:"data",dataModelType:d,dataModelOptions:{name:"kernel.all.cpu.user"},size:{width:"25%",height:"250px"},enableVerticalResize:!1,group:"CPU",attrs:{forcey:1,percentage:!0,integer:!1}},{name:"kernel.all.cpu",title:"CPU Utilization",directive:"area-stacked-time-series",dataAttrName:"data",dataModelType:r,dataModelOptions:{name:"kernel.all.cpu"},size:{width:"25%",height:"250px"},enableVerticalResize:!1,group:"CPU",attrs:{forcey:1,percentage:!0,integer:!1}},{name:"kernel.percpu.cpu.sys",title:"Per-CPU Utilization (System)",directive:"line-time-series",dataAttrName:"data",dataModelType:d,dataModelOptions:{name:"kernel.percpu.cpu.sys"},size:{width:"25%",height:"250px"},enableVerticalResize:!1,group:"CPU",attrs:{forcey:1,percentage:!0,integer:!1}},{name:"kernel.percpu.cpu.user",title:"Per-CPU Utilization (User)",directive:"line-time-series",dataAttrName:"data",dataModelType:d,dataModelOptions:{name:"kernel.percpu.cpu.user"},size:{width:"25%",height:"250px"},enableVerticalResize:!1,group:"CPU",attrs:{forcey:1,percentage:!0,integer:!1}},{name:"kernel.percpu.cpu",title:"Per-CPU Utilization",directive:"line-time-series",dataAttrName:"data",dataModelType:n,dataModelOptions:{name:"kernel.percpu.cpu"},size:{width:"25%",height:"250px"},enableVerticalResize:!1,group:"CPU",attrs:{forcey:1,percentage:!0,integer:!1}},{name:"mem.freemem",title:"Memory Utilization (Free)",directive:"line-time-series",dataAttrName:"data",dataModelType:e,dataModelOptions:{name:"mem.freemem"},size:{width:"25%",height:"250px"},enableVerticalResize:!1,group:"Memory"},{name:"mem.util.used",title:"Memory Utilization (Used)",directive:"line-time-series",dataAttrName:"data",dataModelType:e,dataModelOptions:{name:"mem.util.used"},size:{width:"25%",height:"250px"},enableVerticalResize:!1,group:"Memory"},{name:"mem.util.cached",title:"Memory Utilization (Cached)",directive:"line-time-series",dataAttrName:"data",dataModelType:e,dataModelOptions:{name:"mem.util.cached"},size:{width:"25%",height:"250px"},enableVerticalResize:!1,group:"Memory"},{name:"mem",title:"Memory Utilization",directive:"area-stacked-time-series",dataAttrName:"data",dataModelType:a,dataModelOptions:{name:"mem"},size:{width:"25%",height:"250px"},enableVerticalResize:!1,group:"Memory",attrs:{percentage:!1,integer:!0}},{name:"network.interface.out.drops",title:"Network Drops (Out)",directive:"line-time-series",dataAttrName:"data",dataModelType:e,dataModelOptions:{name:"network.interface.out.drops"},size:{width:"25%",height:"250px"},enableVerticalResize:!1,group:"Network",attrs:{forcey:10,percentage:!1,integer:!0}},{name:"network.interface.in.drops",title:"Network Drops (In)",directive:"line-time-series",dataAttrName:"data",dataModelType:e,dataModelOptions:{name:"network.interface.in.drops"},size:{width:"25%",height:"250px"},enableVerticalResize:!1,group:"Network",attrs:{forcey:10,percentage:!1,integer:!0}},{name:"network.interface.drops",title:"Network Drops",directive:"line-time-series",dataAttrName:"data",dataModelType:o,dataModelOptions:{name:"network.interface.drops",metricDefinitions:{"{key} in":"network.interface.in.drops","{key} out":"network.interface.out.drops"}},size:{width:"25%",height:"250px"},enableVerticalResize:!1,group:"Network",attrs:{forcey:10,percentage:!1,integer:!0}},{name:"network.tcpconn.established",title:"TCP Connections (Estabilished)",directive:"line-time-series",dataAttrName:"data",dataModelType:e,dataModelOptions:{name:"network.tcpconn.established"},size:{width:"25%",height:"250px"},enableVerticalResize:!1,group:"Network",attrs:{percentage:!1,integer:!0}},{name:"network.tcpconn.time_wait",title:"TCP Connections (Time Wait)",directive:"line-time-series",dataAttrName:"data",dataModelType:e,dataModelOptions:{name:"network.tcpconn.time_wait"},enableVerticalResize:!1,size:{width:"25%",height:"250px"},group:"Network",attrs:{percentage:!1,integer:!0}},{name:"network.tcpconn.close_wait",title:"TCP Connections (Close Wait)",directive:"line-time-series",dataAttrName:"data",dataModelType:e,dataModelOptions:{name:"network.tcpconn.close_wait"},size:{width:"25%",height:"250px"},enableVerticalResize:!1,group:"Network",attrs:{percentage:!1,integer:!0}},{name:"network.tcpconn",title:"TCP Connections",directive:"line-time-series",dataAttrName:"data",dataModelType:o,dataModelOptions:{name:"network.tcpconn",metricDefinitions:{established:"network.tcpconn.established",time_wait:"network.tcpconn.time_wait",close_wait:"network.tcpconn.close_wait"}},size:{width:"25%",height:"250px"},enableVerticalResize:!1,group:"Network",attrs:{percentage:!1,integer:!0}},{name:"network.interface.bytes",title:"Network Throughput (kB)",directive:"line-time-series",dataAttrName:"data",dataModelType:i,dataModelOptions:{name:"network.interface.bytes"},size:{width:"25%",height:"250px"},enableVerticalResize:!1,group:"Network",attrs:{percentage:!1,integer:!0}},{name:"disk.iops",title:"Disk IOPS",directive:"line-time-series",dataAttrName:"data",dataModelType:s,dataModelOptions:{name:"disk.iops",metricDefinitions:{"{key} read":"disk.dev.read","{key} write":"disk.dev.write"}},size:{width:"25%",height:"250px"},enableVerticalResize:!1,group:"Disk"},{name:"disk.bytes",title:"Disk Throughput (Bytes)",directive:"line-time-series",dataAttrName:"data",dataModelType:s,dataModelOptions:{name:"disk.bytes",metricDefinitions:{"{key} read":"disk.dev.read_bytes","{key} write":"disk.dev.write_bytes"}},size:{width:"25%",height:"250px"},enableVerticalResize:!1,group:"Disk"},{name:"disk.dev.avactive",title:"Disk Utilization",directive:"line-time-series",dataAttrName:"data",dataModelType:d,dataModelOptions:{name:"disk.dev.avactive"},size:{width:"25%",height:"250px"},enableVerticalResize:!1,group:"Disk",attrs:{forcey:1,percentage:!0,integer:!1}},{name:"kernel.all.pswitch",title:"Context Switches",directive:"line-time-series",dataAttrName:"data",dataModelType:t,dataModelOptions:{name:"kernel.all.pswitch"},size:{width:"25%",height:"250px"},enableVerticalResize:!1,group:"CPU",attrs:{percentage:!1,integer:!0}},{name:"mem.vmstat.pgfault",title:"Page Faults",directive:"area-stacked-time-series",dataAttrName:"data",dataModelType:s,dataModelOptions:{name:"mem.vmstat.pgfault",metricDefinitions:{"page faults":"mem.vmstat.pgfault","major page faults":"mem.vmstat.pgmajfault"}},size:{width:"25%",height:"250px"},enableVerticalResize:!1,group:"Memory",attrs:{percentage:!1,integer:!0}},{name:"network.interface.packets",title:"Network Packets",directive:"line-time-series",dataAttrName:"data",dataModelType:s,dataModelOptions:{name:"network.interface.packets",metricDefinitions:{"{key} in":"network.interface.in.packets","{key} out":"network.interface.out.packets"}},size:{width:"25%",height:"250px"},enableVerticalResize:!1,group:"Network",attrs:{percentage:!1,integer:!0}},{name:"network.tcp.retrans",title:"Network Retransmits",directive:"line-time-series",dataAttrName:"data",dataModelType:s,dataModelOptions:{name:"network.tcp.retrans",metricDefinitions:{retranssegs:"network.tcp.retranssegs",timeouts:"network.tcp.timeouts",listendrops:"network.tcp.listendrops",fastretrans:"network.tcp.fastretrans",slowstartretrans:"network.tcp.slowstartretrans",syncretrans:"network.tcp.syncretrans"}},size:{width:"25%",height:"250px"},enableVerticalResize:!1,group:"Network",attrs:{forcey:10,percentage:!1,integer:!0}},{name:"disk.dev.latency",title:"Disk Latency",directive:"line-time-series",dataAttrName:"data",dataModelType:l,dataModelOptions:{name:"disk.dev.latency"},size:{width:"25%",height:"250px"},enableVerticalResize:!1,group:"Disk",attrs:{percentage:!1,integer:!0}}];return p.enableCpuFlameGraph&&u.push({name:"graph.flame.cpu",title:"CPU Flame Graph",directive:"cpu-flame-graph",dataModelType:c,size:{width:"25%",height:"250px"},enableVerticalResize:!1,group:"CPU"}),p.enableDiskLatencyHeatMap&&u.push({name:"graph.heatmap.disk",title:"Disk Latency Heat Map",directive:"disk-latency-heat-map",dataModelType:c,size:{width:"25%",height:"250px"},enableVerticalResize:!1,group:"Disk"}),u}e.$inject=["MetricDataModel","CumulativeMetricDataModel","MemoryUtilizationMetricDataModel","NetworkBytesMetricDataModel","CpuUtilizationMetricDataModel","PerCpuUtilizationMetricDataModel","MultipleMetricDataModel","MultipleCumulativeMetricDataModel","DummyMetricDataModel","DiskLatencyMetricDataModel","CumulativeUtilizationMetricDataModel","vectorConfig"];var t=[{name:"kernel.all.cpu",size:{width:"25%"}},{name:"kernel.percpu.cpu",size:{width:"25%"}},{name:"kernel.all.runnable",size:{width:"25%"}},{name:"kernel.all.load",size:{width:"25%"}},{name:"network.interface.bytes",size:{width:"25%"}},{name:"network.tcpconn",size:{width:"25%"}},{name:"network.interface.packets",size:{width:"25%"}},{name:"network.tcp.retrans",size:{width:"25%"}},{name:"mem",size:{width:"50%"}},{name:"mem.vmstat.pgfault",size:{width:"25%"}},{name:"kernel.all.pswitch",size:{width:"25%"}},{name:"disk.iops",size:{width:"25%"}},{name:"disk.bytes",size:{width:"25%"}},{name:"disk.dev.avactive",size:{width:"25%"}},{name:"disk.dev.latency",size:{width:"25%"}}],a=[];angular.module("app.widgets",[]).factory("widgetDefinitions",e).value("defaultWidgets",t).value("emptyWidgets",a)}(),/**!
 *
 *  Copyright 2015 Netflix, Inc.
 *
 *     Licensed under the Apache License, Version 2.0 (the "License");
 *     you may not use this file except in compliance with the License.
 *     You may obtain a copy of the License at
 *
 *         http://www.apache.org/licenses/LICENSE-2.0
 *
 *     Unless required by applicable law or agreed to in writing, software
 *     distributed under the License is distributed on an "AS IS" BASIS,
 *     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *     See the License for the specific language governing permissions and
 *     limitations under the License.
 *
 */
function(){"use strict";function e(e){e.when("/",{templateUrl:"app/dashboard/dashboard.html",controller:"DashboardController",controllerAs:"vm",title:"Dashboard - Vector",reloadOnSearch:!1,resolve:{widgets:["defaultWidgets",function(e){return e}]}}).when("/empty",{templateUrl:"app/dashboard/dashboard.html",controller:"DashboardController",controllerAs:"vm",title:"Dashboard - Vector",reloadOnSearch:!1,resolve:{widgets:["emptyWidgets",function(e){return e}]}}).otherwise("/")}e.$inject=["$routeProvider"],angular.module("app.routes").config(e)}(),/**!
 *
 *  Copyright 2015 Netflix, Inc.
 *
 *     Licensed under the Apache License, Version 2.0 (the "License");
 *     you may not use this file except in compliance with the License.
 *     You may obtain a copy of the License at
 *
 *         http://www.apache.org/licenses/LICENSE-2.0
 *
 *     Unless required by applicable law or agreed to in writing, software
 *     distributed under the License is distributed on an "AS IS" BASIS,
 *     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *     See the License for the specific language governing permissions and
 *     limitations under the License.
 *
 */
function(){"use strict";function e(e,t){var a,i,r=[];for(a=0;a<e.length;a++)i=e[a][t],-1===r.indexOf(i)&&r.push(i);return r}function t(){return function(t,a){return null!==t?e(t,a):void 0}}function a(){return function(e,t){return e.filter(function(e){return e.group===t})}}angular.module("app.filters").filter("groupBy",t).filter("groupFilter",a)}(),/**!
 *
 *  Copyright 2015 Netflix, Inc.
 *
 *     Licensed under the Apache License, Version 2.0 (the "License");
 *     you may not use this file except in compliance with the License.
 *     You may obtain a copy of the License at
 *
 *         http://www.apache.org/licenses/LICENSE-2.0
 *
 *     Unless required by applicable law or agreed to in writing, software
 *     distributed under the License is distributed on an "AS IS" BASIS,
 *     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *     See the License for the specific language governing permissions and
 *     limitations under the License.
 *
 */
function(){"use strict";angular.module("vector.config").constant("vectorConfig",{protocol:"http",port:44323,hostspec:"localhost",interval:2,window:2,enableCpuFlameGraph:!1,enableDiskLatencyHeatMap:!1})}(),angular.module("app").run(["$templateCache",function(e){e.put("app/dashboard/dashboard.html",'<div class="row"><div class="col-md-12"><div dashboard="vm.dashboardOptions" template-url="app/dashboard/vector.dashboard.html" class="dashboard-container"></div></div></div>'),e.put("app/dashboard/vector.dashboard.html",'<div class="row"><div class="col-md-6"><form role="form" name="form"><div class="input-group input-group-lg" ng-class="{\'has-error\': form.host.$invalid && form.host.$dirty}"><span class="input-group-addon" ng-click="vm.isHostnameExpanded = !vm.isHostnameExpanded">Hostname &nbsp; <i class="fa fa-plus-square-o"></i></span> <input type="text" class="form-control" id="hostnameInput" name="host" data-content="Please enter the instance hostname. Port can be specified using the <hostname>:<port> format. Expand to change hostspec." rel="popover" data-placement="bottom" data-trigger="hover" data-container="body" ng-model="vm.inputHost" ng-change="vm.updateHost()" ng-model-options="{ updateOn: \'default\', debounce: 1000 }" delay="1000" ng-disabled="flags.contextUpdating == true" required="" placeholder="Instance Hostname"> <i class="fa fa-refresh fa-2x form-control-feedback" ng-if="flags.contextUpdating"></i> <i class="fa fa-check fa-2x form-control-feedback" ng-if="flags.contextAvailable"></i></div></form></div><div class="btn-group col-md-2" id="widgetWrapper"><button id="widgetButton" type="button" class="dropdown-toggle btn btn-lg btn-default btn-block" data-toggle="dropdown">Widget <span class="caret"></span></button><ul class="dropdown-menu" role="menu"><li class="dropdown-submenu" ng-repeat="group in widgetDefs | groupBy: \'group\'"><a ng-click="void(0)" data-toggle="dropdown">{{group}}</a><ul class="dropdown-menu"><li ng-repeat="widget in widgetDefs | groupFilter: group"><a href="#" ng-click="addWidgetInternal($event, widget);">{{widget.title}}</a></li></ul></li><li role="presentation" class="divider"></li><li><a href="javascript:void(0);" ng-click="loadWidgets(defaultWidgets);">Default Widgets</a></li><li><a href="javascript:void(0);" ng-click="loadWidgets(emptyWidgets);">Clear</a></li></ul></div><div class="col-md-2"><form role="form"><div class="input-group input-group-lg" ng-class="{\'has-error\': form.window.$invalid && form.window.$dirty}" style="border-radius: 6 !important;"><span class="input-group-addon">Window</span><select class="form-control" name="window" id="windowInput" data-content="The duration window for all charts in this dashboard." rel="popover" data-placement="bottom" data-trigger="hover" data-container="body" ng-model="properties.window" ng-change="vm.updateWindow()" style="border-radius: 6 !important;"><option value="2">2 min</option><option value="5">5 min</option><option value="10">10 min</option></select></div></form></div><div class="col-md-2"><form role="form"><div class="input-group input-group-lg" ng-class="{\'has-error\': form.interval.$invalid && form.interval.$dirty}"><span class="input-group-addon">Interval</span><select class="form-control" name="interval" id="intervalInput" data-content="The update interval used by all charts in this dashboard." rel="popover" data-placement="bottom" data-trigger="hover" data-container="body" ng-model="properties.interval" ng-change="vm.updateInterval()"><option value="1">1 sec</option><option value="2">2 sec</option><option value="3">3 sec</option><option value="5">5 sec</option></select></div></form></div></div><div class="row" ng-show="vm.isHostnameExpanded"><div class="col-md-6"><form role="form" name="form"><div class="input-group input-group-lg" ng-class="{\'has-error\': form.hostspec.$invalid && form.hostspec.$dirty}"><span class="input-group-addon">Hostspec&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</span> <input type="text" class="form-control" id="hostspecInput" name="host" data-content="Please enter the instance hostspec." rel="popover" data-placement="bottom" data-trigger="hover" data-container="body" ng-model="properties.hostspec" ng-change="vm.updateHost()" ng-model-options="{ updateOn: \'default\', debounce: 1000 }" delay="1000" ng-disabled="flags.contextUpdating == true" required="" placeholder="Instance Hostspec"></div></form></div><div class="col-md-2"><p class="lead" id="hostnameLabel" data-content="PMCD hostname. The hostname from the actual instance you\'re monitoring." rel="popover" data-placement="bottom" data-trigger="hover" data-container="body">{{flags.contextAvailable && properties.hostname ? properties.hostname : \'Disconnected\'}}</p></div></div><div id="alert-dashboard-error" flash-alert="error" active-class="in alert" class="fade"><strong class="alert-heading">Error!</strong> <span class="alert-message">{{flash.message}}</span></div><div class="row"><div class="col-md-12"><div class="btn-toolbar" ng-if="!options.hideToolbar"><div class="btn-group" ng-if="!options.widgetButtons"><button type="button" class="dropdown-toggle btn btn-primary" data-toggle="dropdown">Add Widget <span class="caret"></span></button><ul class="dropdown-menu" role="menu"><li ng-repeat="widget in widgetDefs"><a href="#" ng-click="addWidgetInternal($event, widget);"><span class="label label-primary">{{widget.name}}</span></a></li></ul></div><div class="btn-group" ng-if="options.widgetButtons"><button ng-repeat="widget in widgetDefs" ng-click="addWidgetInternal($event, widget);" type="button" class="btn btn-primary">{{widget.name}}</button></div><button class="btn btn-warning" ng-click="resetWidgetsToDefault()">Default Widgets</button> <button ng-if="options.storage && options.explicitSave" ng-click="options.saveDashboard()" class="btn btn-success" ng-disabled="!options.unsavedChangeCount">{{ !options.unsavedChangeCount ? "all saved" : "save changes (" + options.unsavedChangeCount + ")" }}</button> <button ng-click="clear();" type="button" class="btn btn-info">Clear</button></div><div ui-sortable="sortableOptions" ng-model="widgets" class="dashboard-widget-area"><div ng-repeat="widget in widgets" ng-style="widget.containerStyle" class="widget-container" widget=""><div class="widget panel panel-default"><div class="widget-header panel-heading"><h3 class="panel-title"><span class="widget-title">{{widget.title}}</span><span class="label label-primary" ng-if="!options.hideWidgetName">{{widget.name}}</span> <span ng-click="removeWidget(widget);" class="glyphicon glyphicon-remove" ng-if="!options.hideWidgetClose"></span> <span ng-click="openWidgetDialog(widget);" class="glyphicon glyphicon-cog" ng-if="!options.hideWidgetSettings"></span></h3></div><div class="panel-body widget-content" ng-style="widget.contentStyle"></div><div class="widget-ew-resizer" ng-mousedown="grabResizer($event)"></div><div ng-if="widget.enableVerticalResize" class="widget-s-resizer" ng-mousedown="grabSouthResizer($event)"></div></div></div></div></div></div><script>\n    (function () {\n        \'use strict\';\n        $(\'#hostnameInput\').popover();\n        $(\'#hostspecInput\').popover();\n        $(\'#windowInput\').popover();\n        $(\'#intervalInput\').popover();\n        $(\'#hostnameLabel\').popover();\n    }());\n</script>'),e.put("app/charts/cpu-flame-graph.html",'<div class="col-md-12"><div class="row" style="text-align: center;"><p class="lead" ng-if="!ready">Click on the button below to generate your CPU Flame Graph! (60 sec)</p></div><div class="row" ng-if="!processing && ready" style="text-align: center;"><object data="http://{{host}}:{{port}}/systack/systack.svg" type="image/svg+xml" style="width:100%; height:auto;"></object><br><a class="btn btn-default pull-right" href="http://{{host}}:{{port}}/systack/systack.svg" target="_blank">Open in a new Tab</a></div><div class="row"><div id="alert-sysstack-error" flash-alert="error" active-class="in alert" class="fade"><strong class="alert-heading">Error!</strong> <span class="alert-message">{{flash.message}}</span></div></div><div class="row"><div id="alert-sysstack-success" flash-alert="success" active-class="in alert" class="fade"><strong class="alert-heading">Success!</strong> <span class="alert-message">{{flash.message}}</span></div></div><div class="row" style="text-align: center; margin-top: 15px;"><button type="button" class="btn btn-primary btn-lg" ng-disabled="processing" ng-click="generateFlameGraph()">Generate <i ng-if="processing" class="fa fa-refresh fa-spin"></i></button></div></div>'),e.put("app/charts/disk-latency-heatmap.html",'<div class="col-md-12"><div class="row" style="text-align: center;"><p class="lead" ng-if="!ready">Click on the button below to generate your Disk Latency Heat Map! (140 sec)</p></div><div class="row" ng-if="!processing && ready" style="text-align: center;"><object data="http://{{host}}:{{port}}/heatmap/heatmap.svg" type="image/svg+xml" style="width:100%; height:auto;"></object><br><a class="btn btn-default pull-right" href="http://{{host}}:{{port}}/heatmap/heatmap.svg" target="_blank">Open in a new Tab</a></div><div class="row"><div id="alert-disklatency-error" flash-alert="error" active-class="in alert" class="fade"><strong class="alert-heading">Error!</strong> <span class="alert-message">{{flash.message}}</span></div></div><div class="row"><div id="alert-disklatency-success" flash-alert="success" active-class="in alert" class="fade"><strong class="alert-heading">Success!</strong> <span class="alert-message">{{flash.message}}</span></div></div><div class="row" style="text-align: center; margin-top: 15px;"><button type="button" class="btn btn-primary btn-lg" ng-disabled="processing" ng-click="generateHeatMap()">Generate <i ng-if="processing" class="fa fa-refresh fa-spin"></i></button></div></div>'),e.put("app/charts/nvd3-chart.html",'<div><i class="fa fa-line-chart fa-5x widget-icon" ng-hide="flags.contextAvailable"></i><div id="{{id}}" class="chart"><svg></svg></div></div>')}]);