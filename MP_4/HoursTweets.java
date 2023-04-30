import java.io.IOException;
import java.lang.String;
import java.util.StringTokenizer;

import org.apache.hadoop.conf.Configuration;
import org.apache.hadoop.fs.Path;
import org.apache.hadoop.io.IntWritable;
import org.apache.hadoop.io.Text;
import org.apache.hadoop.mapreduce.Job;
import org.apache.hadoop.mapreduce.Mapper;
import org.apache.hadoop.mapreduce.Reducer;
import org.apache.hadoop.mapreduce.lib.input.FileInputFormat;
import org.apache.hadoop.mapreduce.lib.output.FileOutputFormat;

public class HoursTweets {
    public static class HourMapper extends Mapper<Object, Text, Text, IntWritable> {
        private static final IntWritable one = new IntWritable(1);
        private Text word = new Text();

        public void map(Object key, Text value, Context context) throws IOException, InterruptedException {
            StringTokenizer tkn = new StringTokenizer(value.toString(), "\n");
            while (tkn.hasMoreTokens()) {
                //get hour of day 
                String line = tkn.nextToken();
                if (line.length() != 0 && line.charAt(0)=='T'){
                    // System.out.println(line);
                    StringTokenizer tk = new StringTokenizer(line);
                    if (tk.hasMoreTokens()) tk.nextToken();
                    if (tk.hasMoreTokens()) tk.nextToken();
                    if (tk.hasMoreTokens()){
                        String timestamp = tk.nextToken();
                        String aKey = timestamp.substring(0,2);
                        word.set(aKey);
                        context.write(word, one);
                    }
                }
            }
        }
    }

    public static class HourReducer extends Reducer<Text, IntWritable, Text, IntWritable> {
        private IntWritable sumWrite = new IntWritable();

        public void reduce(Text key, Iterable<IntWritable> values, Context context) throws IOException, InterruptedException {
            int sum = 0;
            for (IntWritable v : values) {
                sum += v.get();
            }
            sumWrite.set(sum);
            context.write(key, sumWrite);
        }
    }

    public static void main(String[] args) throws Exception {
        Configuration conf = new Configuration();
        Job job = Job.getInstance(conf, "tweet count per hour");
        job.setJarByClass(HoursTweets.class);
        job.setMapperClass(HourMapper.class);
        job.setReducerClass(HourReducer.class);
        job.setOutputKeyClass(Text.class);
        job.setOutputValueClass(IntWritable.class);
        FileInputFormat.addInputPath(job, new Path(args[0]));
        FileOutputFormat.setOutputPath(job, new Path(args[1]));
        System.exit(job.waitForCompletion(true) ? 0 : 1);
    }
}
